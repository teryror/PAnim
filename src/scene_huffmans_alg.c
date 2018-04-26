/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "panim.h"

typedef enum CodeTreeType {
    CTT_INVALID,
    CTT_INTERNAL,
    CTT_LEAF,
} CodeTreeType;

typedef struct CodeTree {
    CodeTreeType type;
    int freq;
    union {
        char sym;
        struct {
            struct CodeTree * left;
            struct CodeTree * right;
        } children;
    };
} CodeTree;

static SDL_Texture * circle;
static TTF_Font    * font;
static CodeTree    * huff;

static void
panim_scene_frame_update(PAnimScene * scene, size_t t)
{
    for (PAnimEvent * anim = scene->timeline;
         anim < scene->timeline + buf_len(scene->timeline);
         ++anim)
    {
        panim_event_tick(anim, t);
    }
}

static void
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

// -----------------------

static void print_tree(CodeTree *tree) {
    if (tree->type == CTT_LEAF) {
        printf("(%c %d)", tree->sym, tree->freq);
    } else {
        printf("{ %d ", tree->freq);
        print_tree(tree->children.left);
        printf(" ");
        print_tree(tree->children.right);
        printf(" }");
    }
}

static void print_forest(CodeTree *forest) {
    printf("%zd: ", buf_len(forest));
    for (int i = 0; i < buf_len(forest); ++i) {
        print_tree(&forest[i]);
        printf(" ");
    }
    printf("\n");
}

static CodeTree *
build_huff_tree(char * message) {
    // Count symbol frequencies
    int freqs[256] = {0};
    while (*message) freqs[*message++]++;
    
    // Forest of binary trees, will eventually be combined into a single tree
    CodeTree *forest = NULL;
    for (int i = 0; i < 256; ++i) {
        if (freqs[i] > 0) {
            CodeTree new_node;
            new_node.type = CTT_LEAF;
            new_node.freq = freqs[i];
            new_node.sym  = (char)i;
            
            buf_push(forest, new_node);
        }
    }
    
    /*
    * NOTICE: This is the worst possible implementation of Huffman's algorithm.
    * 
    * A serious attempt would at least use a heap (i.e. a priority queue) in place
    * of a plain array and linear search to keep track of partial code trees.
    * 
    * It doesn't really matter for the animation, and this was the fastest to
    * implement. The fastest to execute would use an entirely different alg anyway.
    *
    * ... also, we leak all the memory here.
    */
    
    print_forest(forest);
    while (buf_len(forest) > 1) {
        // Find two least-frequent nodes
        int min1, min2;
        if (forest[0].freq < forest[1].freq) {
            min1 = 0; min2 = 1;
        } else {
            min1 = 1; min2 = 0;
        }
        
        for (int i = 2; i < buf_len(forest); ++i) {
            if (forest[i].freq < forest[min1].freq) {
                min2 = min1; min1 = i;
            } else if (forest[i].freq < forest[min2].freq) {
                min2 = i;
            }
        }
        
        // Combine nodes
        CodeTree new_node;
        new_node.type = CTT_INTERNAL;
        new_node.freq = forest[min1].freq + forest[min2].freq;
        new_node.children.left  = (CodeTree *) malloc(sizeof(CodeTree));
        new_node.children.right = (CodeTree *) malloc(sizeof(CodeTree));
        *new_node.children.left  = forest[min1];
        *new_node.children.right = forest[min2];
        
        // Remove old nodes and insert new node
        if (min1 < min2) {
            forest[min1] = new_node;
            for (int i = min2 + 1; i < buf_len(forest); ++i) {
                forest[i - 1] = forest[i];
            }
            buf__hdr(forest)->len -= 1;
        } else {
            forest[min2] = new_node;
            for (int i = min1 + 1; i < buf_len(forest); ++i) {
                forest[i - 1] = forest[i];
            }
            buf__hdr(forest)->len -= 1;
        }
        
        print_forest(forest);
    }
    
    return forest;
}

static size_t timeline_cursor = 10;

static void
add_tree_to_scene(PAnimEngine * pnm, PAnimScene * scene,
                  CodeTree * tree,
                  SDL_Rect dst_range)
{
    SDL_Color white = (SDL_Color) { 0xFF, 0xFF, 0xFF, 0xFF };
    
    int x1 = dst_range.x + dst_range.w / 2;
    int y1 = dst_range.y + 50;
    
    if (tree->type == CTT_LEAF) {
        char *lbl = (char *) malloc(2);
        lbl[0] = tree->sym; lbl[1] = 0;
        
        panim_fade_in_text(scene, lbl, font, white, 2, x1, y1, timeline_cursor, 60);
        panim_fade_in_image(scene, circle, 1, x1, y1, timeline_cursor, 60);
        
        timeline_cursor += 30;
    } else if (tree->type == CTT_INTERNAL) {
        SDL_Color line_color = (SDL_Color){ 204, 204, 204, 255 };
        
        panim_fade_in_image(scene, circle, 1, x1, y1, timeline_cursor, 60);
        panim_draw_line(scene, line_color, 0, x1, y1,
                        x1 - dst_range.w / 4, y1 + 100,
                        timeline_cursor + 95, 60);
        panim_draw_line(scene, line_color, 0, x1, y1,
                        x1 + dst_range.w / 4, y1 + 100,
                        timeline_cursor + 95, 60);
        
        char *lbl = (char *) malloc(4);
        snprintf(lbl, 4, "%d", tree->freq);
        panim_fade_in_text(scene, lbl, font, white, 2, x1, y1, timeline_cursor, 60);
        
        timeline_cursor += 30;
        
        SDL_Rect l_range = (SDL_Rect){
            dst_range.x,
            dst_range.y + 100,
            dst_range.w / 2,
            dst_range.h - 100,
        }; add_tree_to_scene(pnm, scene, tree->children.left, l_range);
        
        SDL_Rect r_range = (SDL_Rect){
            dst_range.x + dst_range.w / 2,
            dst_range.y + 100,
            dst_range.w / 2,
            dst_range.h - 100,
        }; add_tree_to_scene(pnm, scene, tree->children.right, r_range);
    }
}

int main(int argc, char *argv[]) {
    // Initialize PAnim
    PAnimScene scene = {0};
    scene.length_in_frames = 0;
    scene.screen_width  = 1280;
    scene.screen_height =  720;
    scene.bg_color = (SDL_Color){ 32, 32, 32, 0xFF };
    
    PAnimEngine pnm = panim_engine_begin_preview(&scene);
    circle = IMG_LoadTexture(pnm.renderer, "circle.png");
    font   = TTF_OpenFont("bin/Oswald-Bold.ttf", 36);
    
    // Populate Scene
    huff = build_huff_tree("ABRACADABRA");
    SDL_Rect dst_range = (SDL_Rect){
        scene.screen_width  / 2 - 570,
        scene.screen_height / 2 - 250,
        1140, 500,
    };
    add_tree_to_scene(&pnm, &scene, huff, dst_range);
    
    // Go!
    panim_scene_finalize(&scene);
    
    if (argc == 1) {
        panim_scene_play(&pnm, &scene);
        return 0;
    } else if (argc != 2) {
        printf("Usage: panim <OutFile>\n");
        return 0;
    }
    
    char * filename = argv[1];
    panim_scene_render(&pnm, &scene, filename);
    
    return 0;
}