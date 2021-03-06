/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "assert.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdint.h"

#include "SDL/SDL.h"
#include "SDL/SDL_image.h"
#include "SDL/SDL_ttf.h"
#undef main

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"

#define ERROR(E) do { fprintf(stderr, "Error: " E "\n"); exit(1); } while (0)

// Stretchy buffers, invented (?) by Sean Barrett, code adapted from
// https://github.com/pervognsen/bitwise/blob/654cd758c421ba8f278d5eee161c91c81d9044b3/ion/common.c#L117-L153

#define MAX(x, y) ((x) >= (y) ? (x) : (y))

typedef struct BufHdr {
    size_t len;
    size_t cap;
    char buf[];
} BufHdr;

#define buf__hdr(b) ((BufHdr *)((char *)(b) - offsetof(BufHdr, buf)))

#define buf_len(b) ((b) ? buf__hdr(b)->len : 0)
#define buf_cap(b) ((b) ? buf__hdr(b)->cap : 0)
#define buf_end(b) ((b) + buf_len(b))
#define buf_sizeof(b) ((b) ? buf_len(b)*sizeof(*b) : 0)

#define buf_free(b) ((b) ? (free(buf__hdr(b)), (b) = NULL) : 0)
#define buf_fit(b, n) ((n) <= buf_cap(b) ? 0 : ((b) = buf__grow((b), (n), sizeof(*(b)))))
#define buf_push(b, ...) (buf_fit((b), 1 + buf_len(b)), (b)[buf__hdr(b)->len++] = (__VA_ARGS__))
#define buf_clear(b) ((b) ? buf__hdr(b)->len = 0 : 0)

void *buf__grow(const void *buf, size_t new_len, size_t elem_size) {
    assert(buf_cap(buf) <= (SIZE_MAX - 1)/2);
    size_t new_cap = MAX(16, MAX(1 + 2*buf_cap(buf), new_len));
    assert(new_len <= new_cap);
    assert(new_cap <= (SIZE_MAX - offsetof(BufHdr, buf))/elem_size);
    size_t new_size = offsetof(BufHdr, buf) + new_cap*elem_size;
    BufHdr *new_hdr;
    if (buf) {
        new_hdr = realloc(buf__hdr(buf), new_size);
    } else {
        new_hdr = malloc(new_size);
        new_hdr->len = 0;
    }
    new_hdr->cap = new_cap;
    return new_hdr->buf;
}

typedef enum PAnimObjType {
    PNM_OBJ_INVALID,
    PNM_OBJ_IMAGE,
    PNM_OBJ_TEXT,
    PNM_OBJ_LINE,
} PAnimObjType;

typedef enum PAnimTextAlignment {
    PNM_TXT_ALIGN_LEFT,
    PNM_TXT_ALIGN_CENTER,
    PNM_TXT_ALIGN_RIGHT,
} PAnimTextAlignment;

typedef struct {
    PAnimObjType type;
    int depth_level;
    SDL_Color color;
    union {
        struct {
            SDL_Texture * texture;
            SDL_Rect location;
        } img;
        struct {
            TTF_Font * font;
            char * data;
            int center_x;
            int center_y;
            PAnimTextAlignment align;
        } txt;
        struct {
            int x1, y1;
            int x2, y2;
        } line;
    };
} PAnimObject;

typedef enum PAnimEventType {
    PNM_EVENT_INVALID,
    PNM_EVENT_COLOR_FADE,
    PNM_EVENT_MOVEMENT,
    PNM_EVENT_COLOCATE,
} PAnimEventType;

typedef struct {
    PAnimEventType type;
    size_t begin_frame;
    size_t length;
    union {
        struct {
            SDL_Color * value;
            SDL_Color new_color;
            SDL_Color old_color; // initialized during playback when the animation begins
        } colfd;
        struct {
            int * x_val;
            int * y_val;
            int x_target;
            int y_target;
            // initialized during playback when the animation begins
            int x_old;
            int y_old;
            bool relative;
        } move;
        struct {
            PAnimObject * src;
            PAnimObject * dst;
            int x_offset;
            int y_offset;
        } copy_pos;
    };
} PAnimEvent;

typedef struct {
    size_t length_in_frames;
    int screen_width;
    int screen_height;
    
    SDL_Color bg_color;
    PAnimObject ** objects;
    PAnimEvent   * timeline;
} PAnimScene;

typedef struct {
    SDL_Window   * window;
    SDL_Renderer * renderer;
} PAnimEngine;

/*
 * Pushes a new image object onto the scene.
 */
static PAnimObject *
panim_scene_add_image(PAnimScene * scene,
                      SDL_Texture * img, SDL_Color mod_color,
                      int center_x, int center_y,
                      int depth_level)
{
    PAnimObject *obj = (PAnimObject *) malloc(sizeof(PAnimObject));
    obj->type = PNM_OBJ_IMAGE;
    obj->depth_level = depth_level;
    obj->color = mod_color;
    obj->img.texture = img;
    
    int w, h; SDL_QueryTexture(img, NULL, NULL, &w, &h);
    obj->img.location = (SDL_Rect){
        .x = center_x - w/2,
        .y = center_y - h/2,
        .w = w, .h = h
    };
    
    buf_push(scene->objects, obj);
    return obj;
}

/*
 * Pushes a new text object onto the scene, taking ownership of `text`.
 */
static PAnimObject *
panim_scene_add_text(PAnimScene * scene,
                     TTF_Font * font, char * text,
                     SDL_Color color,
                     int center_x, int center_y,
                     PAnimTextAlignment alignment,
                     int depth_level)
{
    PAnimObject *obj = (PAnimObject *) malloc(sizeof(PAnimObject));
    obj->type = PNM_OBJ_TEXT;
    obj->depth_level = depth_level;
    obj->color = color;
    obj->txt.font = font;
    obj->txt.data = text;
    obj->txt.center_x = center_x;
    obj->txt.center_y = center_y;
    obj->txt.align = alignment;
    
    buf_push(scene->objects, obj);
    return obj;
}

/*
 * Pushes a new line object onto the scene.
 */
static PAnimObject *
panim_scene_add_line(PAnimScene * scene,
                     SDL_Color color,
                     int x1, int y1,
                     int x2, int y2,
                     int depth_level)
{
    PAnimObject *obj = (PAnimObject *) malloc(sizeof(PAnimObject));
    obj->type = PNM_OBJ_LINE;
    obj->depth_level = depth_level;
    obj->color = color;
    obj->line.x1 = x1;
    obj->line.y1 = y1;
    obj->line.x2 = x2;
    obj->line.y2 = y2;
    
    buf_push(scene->objects, obj);
    return obj;
}

static void
panim_scene_add_fade(PAnimScene * scene,
                     PAnimObject * obj,
                     SDL_Color new_color,
                     size_t begin_frame,
                     size_t length)
{
    PAnimEvent anim;
    anim.type = PNM_EVENT_COLOR_FADE;
    anim.begin_frame = begin_frame;
    anim.length = length;
    anim.colfd.value = &obj->color;
    anim.colfd.new_color = new_color;
    
    size_t anim_end_frame = begin_frame + length;
    if (anim_end_frame > scene->length_in_frames)
        scene->length_in_frames = anim_end_frame;
    
    buf_push(scene->timeline, anim);
}

static void
panim_scene_add_move(PAnimScene * scene, int *x, int *y,
                     int target_x, int target_y, bool relative_move,
                     size_t begin_frame, size_t length)
{
    PAnimEvent anim;
    anim.type = PNM_EVENT_MOVEMENT;
    anim.begin_frame = begin_frame;
    anim.length = length;
    anim.move.x_val = x;
    anim.move.y_val = y;
    anim.move.x_target = target_x;
    anim.move.y_target = target_y;
    anim.move.relative = relative_move;
    
    size_t anim_end_frame = begin_frame + length;
    if (anim_end_frame > scene->length_in_frames)
        scene->length_in_frames = anim_end_frame;
    
    buf_push(scene->timeline, anim);
}

static void
panim_colocate(PAnimScene * scene, PAnimObject * dst, PAnimObject * src,
               int x_offset, int y_offset, size_t begin_frame)
{
    PAnimEvent anim;
    anim.type = PNM_EVENT_COLOCATE;
    anim.begin_frame = begin_frame;
    anim.length = 0;
    anim.copy_pos.x_offset = x_offset;
    anim.copy_pos.y_offset = y_offset;
    anim.copy_pos.dst = dst;
    anim.copy_pos.src = src;
    
    size_t anim_end_frame = begin_frame + 1;
    if (anim_end_frame > scene->length_in_frames)
        scene->length_in_frames = anim_end_frame;
    
    buf_push(scene->timeline, anim);
}

static inline PAnimObject *
panim_fade_in_image(PAnimScene * scene, SDL_Texture * texture,
                    int depth_level, int center_x, int center_y,
                    size_t begin_frame, size_t length)
{
    PAnimObject *img = panim_scene_add_image(
        scene, texture, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0 },
        center_x, center_y, depth_level);
    panim_scene_add_fade(
        scene, img, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF },
        begin_frame, length);
    return img;
}

static inline PAnimObject *
panim_fade_in_text(PAnimScene * scene, char * text,
                   TTF_Font * font, SDL_Color color,
                   int depth_level, int center_x, int center_y,
                   PAnimTextAlignment alignment,
                   size_t begin_frame, size_t length)
{
    PAnimObject *txt = panim_scene_add_text(
        scene, font, text, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0 },
        center_x, center_y, alignment, depth_level);
    panim_scene_add_fade(
        scene, txt, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF },
        begin_frame, length);
    return txt;
}

static inline PAnimObject *
panim_draw_line(PAnimScene * scene, SDL_Color color,
                int depth_level, int x1, int y1, int x2, int y2,
                size_t begin_frame, size_t length)
{
    PAnimObject *line = panim_scene_add_line(
        scene, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0 },
        x1, y1, x1, y1, depth_level);
    
    panim_scene_add_fade(scene, line, color, begin_frame, 2);
    panim_scene_add_move(
        scene, &line->line.x2, &line->line.y2, x2, y2, false, begin_frame, length);
    
    return line;
}

static int
panim_object_depth_sort(const PAnimObject ** a, const PAnimObject ** b)
{
    if ((*a)->depth_level < (*b)->depth_level) return -1;
    if ((*a)->depth_level > (*b)->depth_level) return  1;
    return 0;
}

static int
panim_event_time_sort(const PAnimEvent * a, const PAnimEvent * b)
{
    if (a->begin_frame < b->begin_frame) return -1;
    if (a->begin_frame > b->begin_frame) return  1;
    return 0;
}

static void
panim_scene_finalize(PAnimScene * scene)
{
    // This sort is why scene->objects needs to be an array of pointers.
    // If it were a flat array, any pointers to any of its elements would
    // be invalidated here.        (25 April 2018)
    qsort(scene->objects, buf_len(scene->objects),
          sizeof(PAnimObject *), panim_object_depth_sort);
    
    qsort(scene->timeline, buf_len(scene->timeline),
          sizeof(PAnimEvent), panim_event_time_sort);
}

static inline int
panim_lerp_s32(int a, int b, float t)
{
    return a + (int)(t * ((float)b - (float)a));
}

static inline unsigned char
panim_lerp_u8(unsigned char a, unsigned char b, float t)
{
    return a + (unsigned char)(t * ((float)b - (float)a));
}

static inline SDL_Color
panim_lerp_color(SDL_Color a, SDL_Color b, float t)
{
    SDL_Color result;
    result.r = panim_lerp_u8(a.r, b.r, t);
    result.g = panim_lerp_u8(a.g, b.g, t);
    result.b = panim_lerp_u8(a.b, b.b, t);
    result.a = panim_lerp_u8(a.a, b.a, t);
    return result;
}

static void
panim_event_tick(PAnimEvent * anim, size_t t)
{
    if (t < anim->begin_frame) return;
    if (t > anim->begin_frame + anim->length) return;
    
    if (t == anim->begin_frame) {
        switch (anim->type) {
            case PNM_EVENT_COLOR_FADE: {
                anim->colfd.old_color = *anim->colfd.value;
            } break;
            case PNM_EVENT_MOVEMENT: {
                anim->move.x_old = *anim->move.x_val;
                anim->move.y_old = *anim->move.y_val;
                
                if (anim->move.relative) {
                    anim->move.x_target += anim->move.x_old;
                    anim->move.y_target += anim->move.y_old;
                }
            } break;
            case PNM_EVENT_COLOCATE: {
                PAnimObject *src = anim->copy_pos.src;
                PAnimObject *dst = anim->copy_pos.dst;
                
                int new_x = anim->copy_pos.x_offset;
                int new_y = anim->copy_pos.y_offset;
                if (src->type == PNM_OBJ_IMAGE) {
                    new_x += src->img.location.x + src->img.location.w / 2;
                    new_y += src->img.location.y + src->img.location.h / 2;
                } else if (src->type == PNM_OBJ_TEXT) {
                    new_x += src->txt.center_x;
                    new_y += src->txt.center_y;
                } else if (src->type == PNM_OBJ_LINE) {
                    new_x += (src->line.x1 + src->line.x2) / 2;
                    new_y += (src->line.y1 + src->line.y2) / 2;
                }
                
                if (dst->type == PNM_OBJ_IMAGE) {
                    dst->img.location.x = new_x - dst->img.location.w / 2;
                    dst->img.location.y = new_y - dst->img.location.h / 2;
                } else if (dst->type == PNM_OBJ_TEXT) {
                    dst->txt.center_x = new_x;
                    dst->txt.center_y = new_y;
                } else if (dst->type == PNM_OBJ_LINE) {
                    // Unclear what this would even be used for...?
                    __debugbreak();
                }
            } break;
            default: __debugbreak();
        }
        
        return;
    }
    
    float completion = (float)(t - anim->begin_frame) / (float)(anim->length);
    switch (anim->type) {
        case PNM_EVENT_COLOR_FADE: {
            *anim->colfd.value = panim_lerp_color(
                anim->colfd.old_color, anim->colfd.new_color, completion);
        } break;
        case PNM_EVENT_MOVEMENT: {
            float smoothstep = completion * completion * (3 - 2 * completion);
            
            *anim->move.x_val = panim_lerp_s32(
                anim->move.x_old, anim->move.x_target, smoothstep);
            *anim->move.y_val = panim_lerp_s32(
                anim->move.y_old, anim->move.y_target, smoothstep);
        } break;
        default: __debugbreak();
    }
}

static void
panim_object_draw(PAnimEngine * pnm, PAnimObject * obj)
{
    switch (obj->type) {
        case PNM_OBJ_IMAGE: {
            SDL_SetTextureColorMod(obj->img.texture,
                                   obj->color.r, obj->color.g, obj->color.b);
            SDL_SetTextureAlphaMod(obj->img.texture, obj->color.a);
            
            int ret = SDL_RenderCopy(
                pnm->renderer, obj->img.texture, NULL, &obj->img.location);
        } break;
        case PNM_OBJ_TEXT: {
            // TODO(Optimization): Only redraw text when necessary
            SDL_Surface *surf = TTF_RenderText_Solid(
                obj->txt.font, obj->txt.data, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF });
            SDL_Texture *text = SDL_CreateTextureFromSurface(pnm->renderer, surf);
            SDL_FreeSurface(surf);
            
            SDL_SetTextureBlendMode(text, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(text, obj->color.r, obj->color.g, obj->color.b);
            SDL_SetTextureAlphaMod(text, obj->color.a);
            
            int w, h; SDL_QueryTexture(text, NULL, NULL, &w, &h);
            SDL_Rect location;
            switch (obj->txt.align) {
                case PNM_TXT_ALIGN_LEFT:
                location = (SDL_Rect){
                    .x = obj->txt.center_x,
                    .y = obj->txt.center_y - h/2,
                    .w = w, .h = h,
                }; break;
                case PNM_TXT_ALIGN_CENTER:
                location = (SDL_Rect){
                    .x = obj->txt.center_x - w/2,
                    .y = obj->txt.center_y - h/2,
                    .w = w, .h = h,
                }; break;
                case PNM_TXT_ALIGN_RIGHT:
                location = (SDL_Rect){
                    .x = obj->txt.center_x - w,
                    .y = obj->txt.center_y - h/2,
                    .w = w, .h = h,
                }; break;
            }
            
            SDL_RenderCopy(pnm->renderer, text, NULL, &location);
        } break;
        case PNM_OBJ_LINE: {
            SDL_SetRenderDrawBlendMode(pnm->renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(pnm->renderer,
                                   obj->color.r,
                                   obj->color.g,
                                   obj->color.b,
                                   obj->color.a);
            SDL_RenderDrawLine(pnm->renderer,
                               obj->line.x1, obj->line.y1,
                               obj->line.x2, obj->line.y2);
        } break;
        default: __debugbreak();
    }
}

static PAnimEngine
panim_engine_begin_preview(PAnimScene * scene)
{
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) ERROR("initialization failed (SDL)!");
    if (TTF_Init() != 0) ERROR("initialization failed (TTF)!");
    
    PAnimEngine pnm = {0};
    pnm.window = SDL_CreateWindow(
        "PAnim",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        scene->screen_width,
        scene->screen_height, 0);
    if (pnm.window == NULL) ERROR("failed to create window!");
    
    pnm.renderer = SDL_CreateRenderer(pnm.window, -1, SDL_RENDERER_ACCELERATED);
    if (pnm.renderer == NULL) ERROR("failed to create renderer!");
    
    return pnm;
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
panim_scene_render(PAnimEngine * pnm, PAnimScene * scene, char * filename)
{
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
    
    char title_buffer[1024];
    
    for (size_t t = 0; t < scene->length_in_frames; ++t) {
        snprintf(title_buffer, 1024, "PAnim - Rendering (%zd / %zd)",
                 t, scene->length_in_frames);
        SDL_SetWindowTitle(pnm->window, title_buffer);
        
        
        panim_scene_frame_update(scene, t);
        panim_scene_frame_render(pnm, scene);
        
        // Get backbuffer contents
        fflush(stdout);
        if (av_frame_make_writable(src_frame) < 0)
            ERROR("failed to lock frame buffer!");
        SDL_RenderReadPixels(pnm->renderer, NULL, 0,
                             src_frame->data[0],
                             src_frame->linesize[0]);
        
        // Convert between pixel formats (color spaces)
        sws_scale(sws_ctx,
                  src_frame->data, src_frame->linesize, 0, src_frame->height,
                  dst_frame->data, dst_frame->linesize);
        dst_frame->pts = t;
        
        panim_frame_encode(cdc_ctx, fmt_ctx, stream, dst_frame, packet);
        SDL_RenderPresent(pnm->renderer);
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
    panim_engine_end_preview(pnm);
}

static inline void
panim_scene_frame_update(PAnimScene * scene, size_t t)
{
    for (PAnimEvent * anim = scene->timeline;
         anim < scene->timeline + buf_len(scene->timeline);
         ++anim)
    {
        panim_event_tick(anim, t);
    }
}

static inline void
panim_scene_frame_render(PAnimEngine * pnm, PAnimScene * scene)
{
    SDL_Color bg = scene->bg_color;
    SDL_SetRenderDrawColor(
        pnm->renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(pnm->renderer);
    
    for (int i = 0; i < buf_len(scene->objects); ++i) {
        panim_object_draw(pnm, scene->objects[i]);
    }
}

/* 
* Plays back the scene in a preview window without rendering to a file.
*/
static void
panim_scene_play(PAnimEngine * pnm, PAnimScene * scene)
{
    char title_buffer[1024];
    
    bool paused = false;
    size_t playback_speed = 1;
    
    for (size_t t = 0; t < scene->length_in_frames;) {
        Uint32 ticks_at_start_of_frame = SDL_GetTicks();
        
        SDL_Event event;
        if (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) goto end;
            if (event.type == SDL_KEYUP) switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: {
                    goto end;
                } break;
                case SDLK_SPACE: case 'k': {
                    paused = !paused;
                } break;
                case 'l': {
                    if (playback_speed < 4)
                        playback_speed += 1;
                } break;
                case 'j': {
                    if (playback_speed > 1)
                        playback_speed -= 1;
                } break;
            }
        }
        
        snprintf(title_buffer, 1024, "PAnim - Preview (%zd / %zd)",
                 t, scene->length_in_frames);
        SDL_SetWindowTitle(pnm->window, title_buffer);
        
        if (!paused) for (size_t i = 0; i < playback_speed; ++i) {
            panim_scene_frame_update(scene, t++);
        }
        
        panim_scene_frame_render(pnm, scene);
        
        SDL_RenderPresent(pnm->renderer);
        Uint32 frame_time = SDL_GetTicks() - ticks_at_start_of_frame;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
    }
    
    end: panim_engine_end_preview(pnm);
}

static int
panim_main(int arg_count, char * arg_values[],
           PAnimEngine * pnm, PAnimScene * scene)
{
    panim_scene_finalize(scene);
    if (arg_count == 1) {
        panim_scene_play(pnm, scene);
        return 0;
    } else if (arg_count != 2) {
        printf("Usage: %s <OutFile>\n", arg_values[0]);
        return 0;
    }
    
    char * filename = arg_values[1];
    panim_scene_render(pnm, scene, filename);
    
    return 0;
}