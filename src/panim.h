/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "stdio.h"
#include "stdint.h"

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"
#undef main

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"

#define ERROR(E) do { fprintf(stderr, "Error: " E "\n"); exit(1); } while (0)

typedef struct {
    size_t length_in_frames;
    int screen_width;
    int screen_height;
    
    SDL_Color bg_color;
} PAnimScene;

typedef struct {
    SDL_Window   * window;
    SDL_Renderer * renderer;
} PAnimEngine;

static void
panim_engine_begin_preview(PAnimEngine * pnm, PAnimScene * scene)
{
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) ERROR("initialization failed (SDL)!");
    if (TTF_Init() != 0) ERROR("initialization failed (TTF)!");
    
    pnm->window = SDL_CreateWindow(
        "Hello, SDL!",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        scene->screen_width,
        scene->screen_height, 0);
    if (pnm->window == NULL) ERROR("failed to create window!");
    
    pnm->renderer = SDL_CreateRenderer(pnm->window, -1, SDL_RENDERER_ACCELERATED);
    if (pnm->renderer == NULL) ERROR("failed to create renderer!");
}

static void
panim_engine_end_preview(PAnimEngine * pnm)
{
    SDL_DestroyRenderer(pnm->renderer);
    SDL_DestroyWindow(pnm->window);
    SDL_Quit();
}

static void panim_scene_frame_update(PAnimScene * scene, size_t t);
static void panim_scene_frame_render(PAnimEngine * pnm, PAnimScene * scene);

static AVFrame *
panim_alloc_avframe(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *result = av_frame_alloc();
    if (!result) ERROR("failed to allocate an AVFrame!");
    
    result->format = pix_fmt;
    result->width  = width;
    result->height = height;
    
    if (av_frame_get_buffer(result, 32) < 0) {
        ERROR("failed to get frame buffer!");
    }
    
    return result;
}

static inline void
panim_frame_encode(AVCodecContext * cdc_ctx,
                   AVFormatContext * fmt_ctx, AVStream * stream,
                   AVFrame * frame, AVPacket * packet)
{
    int ret = avcodec_send_frame(cdc_ctx, frame);
    if (ret < 0) ERROR("failed to write frame!");
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(cdc_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) ERROR("error during encoding!");
        
        packet->stream_index = stream->index;
        if (packet->pts != AV_NOPTS_VALUE)
            packet->pts = av_rescale_q(packet->pts, cdc_ctx->time_base, stream->time_base);
        if (packet->dts != AV_NOPTS_VALUE)
            packet->dts = av_rescale_q(packet->dts, cdc_ctx->time_base, stream->time_base);
        
        if (av_write_frame(fmt_ctx, packet) < 0) {
            ERROR("failed to write frame to stream");
        }
        
        av_packet_unref(packet);
    }
}

/* 
 * Plays back the scene in a preview window while also rendering it to a file.
 */
static void
panim_scene_render(PAnimScene * scene, char * filename)
{
    PAnimEngine pnm = {0};
    panim_engine_begin_preview(&pnm, scene);
    
    // 
    // Video Encoding Setup
    // 
    
    av_register_all();
    avcodec_register_all();
    
    AVFormatContext *fmt_ctx = NULL;
    avformat_alloc_output_context2(&fmt_ctx, NULL, "mp4", filename);
    if (!fmt_ctx) ERROR("failed to allocate an AVFormatContext");
    
    AVOutputFormat *fmt = fmt_ctx->oformat;
    fmt->video_codec = AV_CODEC_ID_H264;
    fmt->audio_codec = AV_CODEC_ID_NONE;
    
    AVCodec *codec = avcodec_find_encoder(fmt->video_codec);
    if (!codec) ERROR("codec not found!");
    
    AVStream *stream = avformat_new_stream(fmt_ctx, codec);
    if (!stream) ERROR("failed to add video stream to container!");
    stream->id = fmt_ctx->nb_streams - 1;
    
    AVCodecContext *cdc_ctx = stream->codec;
    { // Set codec parameters:
        // TODO: specifying codec parameters like this is deprecated,
        // you're supposed to use stream->codecpar, apparently, but I don't see
        // how you'd specify most of these on that
        
        // TODO: codec-specific options (lossless?, CABAC?)
        // I think you need to pass an AVOptions thing as the 3rd arg to
        // avcodec_open2, but I can't seem to find out how to actually make one?
        
        cdc_ctx->width  = scene->screen_width;
        cdc_ctx->height = scene->screen_height;
        
        // I don't know why you're supposed to do this, since avformat_write_header
        // overwrites it with some ridiculous value later on, which is why we need
        // to rescale our packets' timestamps in the main loop.
        stream->time_base = (AVRational){1, 60};
        
        cdc_ctx->time_base = (AVRational){1, 60};
        cdc_ctx->framerate = (AVRational){60, 1};
        
        // YouTube recommends GOP of half the frame rate,
        // i.e. at most one intra frame every thirty frames
        cdc_ctx->gop_size = 30;
        cdc_ctx->max_b_frames = 2;
        cdc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        cdc_ctx->bit_rate = 12000000; // Recommended bitrate for 1080p60 SDR
        
        if (fmt->flags & AVFMT_GLOBALHEADER)
            cdc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    if (avcodec_open2(cdc_ctx, codec, NULL) < 0) ERROR("failed to open codec!");
    
    AVFrame *src_frame = panim_alloc_avframe(
        AV_PIX_FMT_RGB32, cdc_ctx->width, cdc_ctx->height);
    AVFrame *dst_frame = panim_alloc_avframe(
        cdc_ctx->pix_fmt, cdc_ctx->width, cdc_ctx->height);
    
    struct SwsContext *sws_ctx = sws_getContext(
        src_frame->width, src_frame->height, src_frame->format,
        dst_frame->width, dst_frame->height, dst_frame->format,
        0, 0, 0, 0);
    if (!sws_ctx) ERROR("failed to get an SwsContext!");
    
    AVPacket *packet = av_packet_alloc();
    if (!packet) ERROR("failed to allocate an AVPacket!");
    
    // Open the output file if needed
    if (!(fmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) {
            ERROR("failed to open output file!");
        }
    }
    
    if (avformat_write_header(fmt_ctx, NULL) < 0) {
        ERROR("failed to write file header!");
    }
    
    // 
    // Main Loop
    // 
    
    for (size_t t = 0; t < scene->length_in_frames; ++t) {
        panim_scene_frame_update(scene, t);
        panim_scene_frame_render(&pnm, scene);
        
        // Get backbuffer contents
        fflush(stdout);
        if (av_frame_make_writable(src_frame) < 0)
            ERROR("failed to lock frame buffer!");
        SDL_RenderReadPixels(pnm.renderer, NULL, 0,
                             src_frame->data[0],
                             src_frame->linesize[0]);
        
        // Convert between pixel formats (color spaces)
        sws_scale(sws_ctx,
                  src_frame->data, src_frame->linesize, 0, src_frame->height,
                  dst_frame->data, dst_frame->linesize);
        dst_frame->pts = t;
        
        panim_frame_encode(cdc_ctx, fmt_ctx, stream, dst_frame, packet);
        SDL_RenderPresent(pnm.renderer);
    }
    
    panim_frame_encode(cdc_ctx, fmt_ctx, stream, NULL, packet); // Flush the encoder
    av_write_trailer(fmt_ctx);
    
    // Close the output stream
    avcodec_close(stream->codec);
    av_frame_free(&src_frame);
    av_frame_free(&dst_frame);
    sws_freeContext(sws_ctx);
    
    if (!(fmt->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb);
    avformat_free_context(fmt_ctx);
    
    //---
    panim_engine_end_preview(&pnm);
}

/* 
* Plays back the scene in a preview window without rendering to a file.
*/
static void
panim_scene_play(PAnimScene * scene)
{
    PAnimEngine pnm = {0};
    panim_engine_begin_preview(&pnm, scene);
    
    for (size_t t = 0; t < scene->length_in_frames; ++t) {
        Uint32 ticks_at_start_of_frame = SDL_GetTicks();
        
        panim_scene_frame_update(scene, t);
        panim_scene_frame_render(&pnm, scene);
        
        SDL_RenderPresent(pnm.renderer);
        Uint32 frame_time = SDL_GetTicks() - ticks_at_start_of_frame;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
    }
    
    panim_engine_end_preview(&pnm);
}
