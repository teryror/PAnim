/***********************************************************
 Video Encoding Test with FFmpeg libavcodec
 
 The goal is to produce a 1 second video file fading
 from black to pink with YouTube's recommended encoding
 
 To build:
     cl vid_test.c /Febin\vid_test.exe /Iinclude /link /libpath:lib avcodec.lib avutil.lib
***********************************************************/

#include "stdio.h"
#include "libavcodec/avcodec.h"

#define ERROR(E) do { fprintf(stderr, "Error: " E "\n"); exit(1); } while (0)

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
    
    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (!context) ERROR("failed to allocate an AVCodecContext!");
    
    { // Set codec parameters:
        context->bit_rate = 400000;
        context->width  = 1280;
        context->height = 720;
        context->time_base = (AVRational){1, 60};
        context->framerate = (AVRational){60, 1};
        
        context->gop_size = 10; // 1 intra frame every 10 frames
        context->max_b_frames = 1;
        context->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    
    if (avcodec_open2(context, codec, NULL) < 0) ERROR("failed to open codec!");
    
    AVFrame  *frame  = av_frame_alloc();
    if (!frame) ERROR("failed to allocate an AVFrame!");
    frame->format = context->pix_fmt;
    frame->width  = context->width;
    frame->height = context->height;
    if (av_frame_get_buffer(frame, 32) < 0) ERROR("failed to get frame buffer!");
    
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
        
        int ret = av_frame_make_writable(frame);
        if (ret < 0) {
            fprintf(stderr, "Error: failed to generate frame!\nAVERROR %d: %s\n",
                    ret, av_err2str(ret));
            exit(1);
        }
        
        char t = (char)((float)i / 60.0f * 127);
        for (int y = 0; y < context->height; ++y) {
            for (int x = 0; x < context->width; ++x) {
                frame->data[0][y * frame->linesize[0] + x] = t;
            }
        }
        
        for (int y = 0; y < context->height / 2; ++y) {
            for (int x = 0; x < context->width / 2; ++x) {
                char S = 128 + t;
                frame->data[1][y * frame->linesize[1] + x] = S;
                frame->data[2][y * frame->linesize[2] + x] = S;
            }
        }
        
        frame->pts = i;
        frame_encode_test(context, frame, packet, f);
    }
    
    printf("DONE.                   ");
    fflush(stdout);
    
    frame_encode_test(context, NULL, packet, f); // flush the encoder
    
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);
    
    avcodec_free_context(&context);
    av_frame_free(&frame);
    av_packet_free(&packet);
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