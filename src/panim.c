/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "stdio.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"

#define ERROR(E) do { fprintf(stderr, "Error: " E "\n"); exit(1); } while (0)

static void frame_convert_test(struct SwsContext *context,
                               AVFrame *src, AVFrame *dst,
                               AVPacket *packet)
{
    sws_scale(context, src->data, src->linesize, 0, src->height,
              dst->data, dst->linesize);
}

static void frame_encode_test(AVCodecContext *context,
                              AVFrame  *frame,
                              AVPacket *packet,
                              FILE *outfile)
{
    int ret = avcodec_send_frame(context, frame);
    if (ret < 0) ERROR("failed to send frame for encoding!");
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        } else if (ret < 0) ERROR("error during encoding!");
        
        fwrite(packet->data, 1, packet->size, outfile);
        av_packet_unref(packet);
    }
}

static void video_encode_test(char * filename) {
    avcodec_register_all();
    
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) ERROR("codec not found!");
    
    AVCodecContext *cdc_ctx = avcodec_alloc_context3(codec);
    if (!cdc_ctx) ERROR("failed to allocate an AVCodecContext!");
    
    { // Set codec parameters:
        cdc_ctx->bit_rate = 400000;
        cdc_ctx->width  = 1280;
        cdc_ctx->height = 720;
        cdc_ctx->time_base = (AVRational){1, 60};
        cdc_ctx->framerate = (AVRational){60, 1};
        
        cdc_ctx->gop_size = 10; // 1 intra frame every 10 frames
        cdc_ctx->max_b_frames = 1;
        cdc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    
    if (avcodec_open2(cdc_ctx, codec, NULL) < 0) ERROR("failed to open codec!");
    
    AVFrame  *src_frame  = av_frame_alloc();
    if (!src_frame) ERROR("failed to allocate an AVFrame!");
    src_frame->format = AV_PIX_FMT_RGB24;
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
    
    // 60 frames for one second of footage
    for (int i = 0; i < 60; ++i) {
        printf("Encoding frame %2d/60 ...\r", i);
        fflush(stdout);
        
        int ret = av_frame_make_writable(src_frame);
        if (ret < 0) {
            fprintf(stderr, "Error: failed to generate frame!\nAVERROR %d: %s\n",
                    ret, av_err2str(ret));
            exit(1);
        }
        
        char t = (char)((float)i / 60.0f * 255);
        for (int y = 0; y < cdc_ctx->height; ++y) {
            for (int x = 0; x < cdc_ctx->width; ++x) {
                src_frame->data[0][y * src_frame->linesize[0] + 3*x + 0] = t;
                src_frame->data[0][y * src_frame->linesize[0] + 3*x + 1] = 0;
                src_frame->data[0][y * src_frame->linesize[0] + 3*x + 2] = t;
            }
        }
        
        src_frame->pts = i;
        
        frame_convert_test(sws_ctx, src_frame, dst_frame, packet);
        frame_encode_test(cdc_ctx, dst_frame, packet, f);
    }
    
    printf("DONE.                   ");
    fflush(stdout);
    
    frame_encode_test(cdc_ctx, NULL, packet, f); // flush the encoder
    
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);
    
    av_frame_free(&src_frame);
    av_frame_free(&dst_frame);
    av_packet_free(&packet);
    avcodec_free_context(&cdc_ctx);
    sws_freeContext(sws_ctx);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: vid_test <OutFile>\n");
        return 0;
    }
    
    char * filename = argv[1];
    video_encode_test(filename);
    
    return 0;
}
