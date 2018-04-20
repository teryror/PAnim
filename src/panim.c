/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "stdio.h"
#include "stdint.h"

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"
#undef main

#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"

#define ERROR(E) do { fprintf(stderr, "Error: " E "\n"); exit(1); } while (0)
#define VidWidth 1280
#define VidHeight 720

typedef struct {
    size_t length_in_frames;
    int screen_width;
    int screen_height;
    
    SDL_Color bg_color;
} PAnimScene;

typedef struct {
    SDL_Window   * window;
    SDL_Renderer * renderer;
    SDL_Texture  * render_target;
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
    
    pnm->render_target = SDL_CreateTexture(
        pnm->renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,
        scene->screen_width,
        scene->screen_height);
    if (pnm->render_target == NULL) ERROR("failed to create render target!");
}

static void
panim_engine_end_preview(PAnimEngine * pnm)
{
    SDL_DestroyRenderer(pnm->renderer);
    SDL_DestroyWindow(pnm->window);
    SDL_Quit();
}

static void
panim_scene_frame_update(PAnimScene * scene, size_t t)
{
    unsigned char col = (unsigned char)
        ((double)t / (double)scene->length_in_frames * 255.0);
    scene->bg_color = (SDL_Color){ col, 0, col, 255 };
    
    // TODO: Check timeline for animations and update scene objects accordingly
}

static void
panim_scene_frame_render(PAnimEngine * pnm, PAnimScene * scene)
{
    SDL_SetRenderTarget(pnm->renderer, pnm->render_target);
    SDL_Color bg = scene->bg_color;
    SDL_SetRenderDrawColor(
        pnm->renderer, bg.r, bg.g, bg.b, bg.a);
    
    // TODO: Render all objects in scene
    
    SDL_RenderClear(pnm->renderer);
}

static void
panim_frame_present(PAnimEngine * pnm)
{
    SDL_SetRenderTarget(pnm->renderer, NULL);
    SDL_RenderClear(pnm->renderer);
    SDL_RenderCopy(pnm->renderer, pnm->render_target, NULL, NULL);
    
    // TODO: Debug UI?
    
    SDL_RenderPresent(pnm->renderer);
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
    
    avcodec_register_all();
    
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) ERROR("codec not found!");
    
    AVCodecContext *cdc_ctx = avcodec_alloc_context3(codec);
    if (!cdc_ctx) ERROR("failed to allocate an AVCodecContext!");
    
    { // Set codec parameters:
        cdc_ctx->bit_rate = 400000;
        cdc_ctx->width  = VidWidth;
        cdc_ctx->height = VidHeight;
        cdc_ctx->time_base = (AVRational){1, 60};
        cdc_ctx->framerate = (AVRational){60, 1};
        
        cdc_ctx->gop_size = 10; // 1 intra frame every 10 frames
        cdc_ctx->max_b_frames = 1;
        cdc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    
    if (avcodec_open2(cdc_ctx, codec, NULL) < 0) ERROR("failed to open codec!");
    
    AVFrame  *src_frame  = av_frame_alloc();
    if (!src_frame) ERROR("failed to allocate an AVFrame!");
    src_frame->format = AV_PIX_FMT_RGB32;
    src_frame->width  = cdc_ctx->width;
    src_frame->height = cdc_ctx->height;
    if (av_frame_get_buffer(src_frame, 32) < 0) ERROR("failed to get frame buffer!");
    
    AVFrame  *dst_frame  = av_frame_alloc();
    if (!dst_frame) ERROR("failed to allocate an AVFrame!");
    dst_frame->format = cdc_ctx->pix_fmt;
    dst_frame->width  = cdc_ctx->width;
    dst_frame->height = cdc_ctx->height;
    if (av_frame_get_buffer(dst_frame, 32) < 0) ERROR("failed to get frame buffer!");
    
    struct SwsContext *sws_ctx = sws_getContext(
        src_frame->width, src_frame->height, src_frame->format,
        dst_frame->width, dst_frame->height, dst_frame->format,
        0, 0, 0, 0);
    if (!sws_ctx) ERROR("failed to get an SwsContext!");
    
    AVPacket *packet = av_packet_alloc();
    if (!packet) ERROR("failed to allocate an AVPacket!");
    
    FILE *f = fopen(filename, "wb");
    if (!f) {
        printf("Error: failed to open file '%s'!\n", filename);
        exit(1);
    }
    
    // 
    // Main Loop
    // 
    
    for (size_t t = 0; t < scene->length_in_frames; ++t) {
        panim_scene_frame_update(scene, t);
        panim_scene_frame_render(&pnm, scene);
        
        { // Get backbuffer contents
            fflush(stdout);
            
            int ret = av_frame_make_writable(src_frame);
            if (ret < 0) {
                fprintf(stderr, "Error: failed to generate frame!\nAVERROR %d: %s\n",
                        ret, av_err2str(ret));
                exit(1);
            }
            
            SDL_RenderReadPixels(pnm.renderer, NULL, 0,
                                 src_frame->data[0],
                                 src_frame->linesize[0]);
            src_frame->pts = t;
        }
        
        // Convert between pixel formats (color spaces)
        sws_scale(sws_ctx,
                  src_frame->data, src_frame->linesize, 0, src_frame->height,
                  dst_frame->data, dst_frame->linesize);
        
        { // Send frame for encoding
            int ret = avcodec_send_frame(cdc_ctx, dst_frame);
            if (ret < 0) ERROR("failed to send frame for encoding!");
            
            while (ret >= 0) {
                ret = avcodec_receive_packet(cdc_ctx, packet);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) ERROR("error during encoding!");
                
                fwrite(packet->data, 1, packet->size, f);
                av_packet_unref(packet);
            }
        }
        
        panim_frame_present(&pnm);
    }
    
    { // Flush the encoder
        fflush(stdout);
        int ret = avcodec_send_frame(cdc_ctx, NULL);
        if (ret < 0) ERROR("failed to send frame for encoding!");
        
        while (ret >= 0) {
            ret = avcodec_receive_packet(cdc_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) ERROR("error during encoding!");
            
            fwrite(packet->data, 1, packet->size, f);
            av_packet_unref(packet);
        }
    }
    
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);
    
    av_frame_free(&src_frame);
    av_frame_free(&dst_frame);
    av_packet_free(&packet);
    avcodec_free_context(&cdc_ctx);
    sws_freeContext(sws_ctx);
    
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
        panim_scene_frame_update(scene, t);
        panim_scene_frame_render(&pnm, scene);
        panim_frame_present(&pnm);
        
        // TODO: Limit framerate for preview-only mode
        SDL_Delay(15);
    }
    
    panim_engine_end_preview(&pnm);
}

int main(int argc, char *argv[]) {
    PAnimScene scene = {0};
    scene.length_in_frames = 240;
    scene.screen_width  = 1280;
    scene.screen_height =  720;
    
    if (argc == 1) {
        panim_scene_play(&scene);
        return 0;
    } else if (argc != 2) {
        printf("Usage: panim <OutFile>\n");
        return 0;
    }
    
    char * filename = argv[1];
    panim_scene_render(&scene, filename);
    
    return 0;
}
