/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "stdio.h"

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"
#undef main

#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"

#define ERROR(E) do { fprintf(stderr, "Error: " E "\n"); exit(1); } while (0)
#define VidWidth 1280
#define VidHeight 720

static void frame_convert_test(struct SwsContext *context,
                               AVFrame *src, AVFrame *dst)
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
    // 
    // Rendering Setup
    // 
    
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) ERROR("initialization failed (SDL)!");
    if (TTF_Init() != 0) ERROR("initialization failed (TTF)!");
    
    SDL_Window *window = SDL_CreateWindow(
        "Hello, SDL!",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        VidWidth, VidHeight, 0);
    if (window == NULL) ERROR("failed to create window!");
    
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) ERROR("failed to create renderer!");
    
    SDL_Texture *text = NULL;
    TTF_Font *font = TTF_OpenFont("Oswald-Bold.ttf", 48);
    
    SDL_Texture *vid_frame_texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,
        VidWidth, VidHeight);
    if (vid_frame_texture == NULL) ERROR("failed to create render target!");
    
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
    
    // 60 frames for one second of footage
    for (int i = 0; i < 240; ++i) {
        char t = (char)((float)i / 240.0f * 255);
        /*
        SDL_Event e;
        if (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
            if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_ESCAPE) break;
        }
        */
        { // Render Text
            SDL_Surface *surf = TTF_RenderText_Solid(
                font, "Hello, World!", (SDL_Color){ t, t, t, 255 });
            text = SDL_CreateTextureFromSurface(renderer, surf);
            
            SDL_FreeSurface(surf);
        }
        
        int w, h;
        SDL_QueryTexture(text, NULL, NULL, &w, &h);
        SDL_Rect dst_rect = (SDL_Rect){ VidWidth/2 - w/2, VidHeight/2 - h/2, w, h }; 
        
        SDL_SetRenderTarget(renderer, vid_frame_texture);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, text, NULL, &dst_rect);
        
        { // Encode content of the current render target
            printf("Encoding frame %2d/240 ...\r", i);
            fflush(stdout);
            
            int ret = av_frame_make_writable(src_frame);
            if (ret < 0) {
                fprintf(stderr, "Error: failed to generate frame!\nAVERROR %d: %s\n",
                        ret, av_err2str(ret));
                exit(1);
            }
            
            SDL_RenderReadPixels(renderer, NULL, 0,
                                 src_frame->data[0],
                                 src_frame->linesize[0]);
            src_frame->pts = i;
            
            frame_convert_test(sws_ctx, src_frame, dst_frame);
            frame_encode_test(cdc_ctx, dst_frame, packet, f);
        }
        
        SDL_SetRenderTarget(renderer, NULL);
        SDL_RenderClear(renderer);
        if (SDL_RenderCopy(renderer, vid_frame_texture, NULL, NULL)) {
            printf("\nSomething happened here!\n");
        }
        SDL_RenderCopy(renderer, text, NULL, &(SDL_Rect){ 0, 0, w/2, h/2 });
        SDL_RenderPresent(renderer);
    }
    
    // 
    // Final Touches
    // 
    
    printf("DONE.                     ");
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
    
    TTF_CloseFont(font);
    SDL_DestroyTexture(text);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
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
