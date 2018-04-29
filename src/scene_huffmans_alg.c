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
        struct {
            char symbol;
            PAnimObject * bgi;
            PAnimObject * txt;
            PAnimObject * cnt;
        } sym;
        struct {
            PAnimObject * node_bg;
            PAnimObject * node_txt;
            
            PAnimObject * linel;
            PAnimObject * lbl_l;
            struct CodeTree * left;
            PAnimObject * liner;
            PAnimObject * lbl_r;
            struct CodeTree * right;
        } children;
    };
} CodeTree;

static SDL_Texture * circle;
static TTF_Font    * font;

// -----------------------

static size_t timeline_cursor = 10;

static void
move_tree(PAnimScene * scene, CodeTree * tree,
          int offset_x, int offset_y,
          size_t begin_frame, size_t length)
{
    if (tree->type == CTT_LEAF) {
        panim_scene_add_move(
            scene, &tree->sym.bgi->img.location.x, &tree->sym.bgi->img.location.y,
            offset_x, offset_y, true, begin_frame, length);
        panim_scene_add_move(
            scene, &tree->sym.txt->txt.center_x, &tree->sym.txt->txt.center_y,
            offset_x, offset_y, true, begin_frame, length);
        panim_scene_add_move(
            scene, &tree->sym.cnt->txt.center_x, &tree->sym.cnt->txt.center_y,
            offset_x, offset_y, true, begin_frame, length);
    } else if (tree->type == CTT_INTERNAL) {
        panim_scene_add_move(
            scene, &tree->children.node_bg->img.location.x,
            &tree->children.node_bg->img.location.y,
            offset_x, offset_y, true, begin_frame, length);
        panim_scene_add_move(
            scene, &tree->children.node_txt->txt.center_x,
            &tree->children.node_txt->txt.center_y,
            offset_x, offset_y, true, begin_frame, length);
        if (tree->children.lbl_l) {
            panim_scene_add_move(
                scene, &tree->children.lbl_l->txt.center_x,
                &tree->children.lbl_l->txt.center_y,
                offset_x, offset_y, true, begin_frame, length);
        }
        panim_scene_add_move(
            scene, &tree->children.linel->line.x1,
            &tree->children.linel->line.y1,
            offset_x, offset_y, true, begin_frame, length);
        panim_scene_add_move(
            scene, &tree->children.linel->line.x2,
            &tree->children.linel->line.y2,
            offset_x, offset_y, true, begin_frame, length);
        if (tree->children.lbl_r) {
            panim_scene_add_move(
                scene, &tree->children.lbl_r->txt.center_x,
                &tree->children.lbl_r->txt.center_y,
                offset_x, offset_y, true, begin_frame, length);
        }
        panim_scene_add_move(
            scene, &tree->children.liner->line.x1,
            &tree->children.liner->line.y1,
            offset_x, offset_y, true, begin_frame, length);
        panim_scene_add_move(
            scene, &tree->children.liner->line.x2,
            &tree->children.liner->line.y2,
            offset_x, offset_y, true, begin_frame, length);
        
        move_tree(
            scene, tree->children.left, offset_x, offset_y, begin_frame, length);
        move_tree(
            scene, tree->children.right, offset_x, offset_y, begin_frame, length);
    }
}

static CodeTree *
build_huff_tree(PAnimScene * scene, char * message) {
    SDL_Color line_color = (SDL_Color){ 0xC8, 0xC8, 0xC8, 0xFF };
    
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
            new_node.sym.symbol  = (char)i;
            
            new_node.sym.bgi = panim_fade_in_image(
                scene, circle, 1, 340 + 100 * ((int)buf_len(forest) + 1), 100,
                timeline_cursor, 30);
            
            char *lbl = (char *) malloc(2);
            lbl[0] = (char)i; lbl[1] = 0;
            new_node.sym.txt = panim_fade_in_text(
                scene, lbl, font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF }, 2,
                340 + 100 * ((int)buf_len(forest) + 1), 100, PNM_TXT_ALIGN_CENTER,
                timeline_cursor, 30);
            
            lbl = (char *) malloc(2);
            snprintf(lbl, 2, "%d", freqs[i]);
            new_node.sym.cnt = panim_fade_in_text(
                scene, lbl, font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF }, 2,
                340 + 100 * ((int)buf_len(forest) + 1), 170, PNM_TXT_ALIGN_CENTER,
                timeline_cursor, 30);
            
            timeline_cursor += 15;
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
        CodeTree new_node = {0};
        new_node.type = CTT_INTERNAL;
        new_node.freq = forest[min1].freq + forest[min2].freq;
        new_node.children.left  = (CodeTree *) malloc(sizeof(CodeTree));
        new_node.children.right = (CodeTree *) malloc(sizeof(CodeTree));
        
        move_tree(scene, &forest[min1], 0, 100, timeline_cursor, 30);
        move_tree(scene, &forest[min2], 0, 100, timeline_cursor, 30);
        timeline_cursor += 30;
        
        int xl, xr, write_idx;
        // Remove old nodes and insert new node
        if (min1 < min2) {
            *new_node.children.left  = forest[min1];
            *new_node.children.right = forest[min2];
            
            xl = (forest[min1].type == CTT_LEAF) ? forest[min1].sym.txt->txt.center_x : forest[min1].children.node_txt->txt.center_x;
            xr = (forest[min2].type == CTT_LEAF) ? forest[min2].sym.txt->txt.center_x : forest[min2].children.node_txt->txt.center_x;
            write_idx = min1;
            
            for (int i = min2 + 1; i < buf_len(forest); ++i) {
                forest[i - 1] = forest[i];
            }
            buf__hdr(forest)->len -= 1;
        } else {
            *new_node.children.left  = forest[min2];
            *new_node.children.right = forest[min1];
            
            xl = (forest[min2].type == CTT_LEAF) ? forest[min2].sym.txt->txt.center_x : forest[min2].children.node_txt->txt.center_x;
            xr = (forest[min1].type == CTT_LEAF) ? forest[min1].sym.txt->txt.center_x : forest[min1].children.node_txt->txt.center_x;
            write_idx = min2;
            
            for (int i = min1 + 1; i < buf_len(forest); ++i) {
                forest[i - 1] = forest[i];
            }
            buf__hdr(forest)->len -= 1;
        }
        
        new_node.children.node_bg = panim_fade_in_image(
            scene, circle, 1, (xl + xr) / 2, 100, timeline_cursor, 60);
        
        char *lbl = (char *) malloc(4);
        snprintf(lbl, 4, "%d", new_node.freq);
        new_node.children.node_txt = panim_fade_in_text(
            scene, lbl, font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF }, 2,
            (xl + xr) / 2, 100, PNM_TXT_ALIGN_CENTER, timeline_cursor, 60);
        timeline_cursor += 45;
        
        new_node.children.linel = panim_draw_line(
            scene, line_color, 0, (xl + xr) / 2, 100, xl, 200,
            timeline_cursor, 60);
        new_node.children.liner = panim_draw_line(
            scene, line_color, 0, (xl + xr) / 2, 100, xr, 200,
            timeline_cursor, 60);
        
        forest[write_idx] = new_node;
        timeline_cursor += 60;
    }
    
    return forest;
}

static void
add_tree_labels(PAnimScene * scene, CodeTree * tree) {
    if (tree->type == CTT_LEAF) return;
    
    tree->children.lbl_l = panim_fade_in_text(
        scene, "0", font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF },
        4, 0, 0, PNM_TXT_ALIGN_CENTER, timeline_cursor, 20);
    panim_colocate(
        scene, tree->children.lbl_l,
        tree->children.linel,
        -25, -10, timeline_cursor);
    
    timeline_cursor += 10;
    tree->children.lbl_r = panim_fade_in_text(
        scene, "1", font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF },
        4, 0, 0, PNM_TXT_ALIGN_CENTER, timeline_cursor, 20);
    panim_colocate(
        scene, tree->children.lbl_r,
        tree->children.liner, 
        25, -10, timeline_cursor);
    
    add_tree_labels(scene, tree->children.left);
    timeline_cursor += 10;
    add_tree_labels(scene, tree->children.right);
    timeline_cursor -= 30;
}

static int code_word_table_y = 100;

static void
add_code_words(PAnimScene * scene, CodeTree * tree, int codeword, int codelen) {
    if (tree->type == CTT_LEAF) {
        char * code = (char *) malloc(codelen + 4);
        code[0] = tree->sym.symbol;
        code[1] = ':';
        code[2] = ' ';
        for (int i = 0; i < codelen; ++i) {
            if (codeword & (1 << (codelen - 1 - i))) {
                code[3+i] = '1';
            } else {
                code[3+i] = '0';
            }
        }
        
        panim_fade_in_text(scene, code, font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF },
                           5, 900, code_word_table_y, PNM_TXT_ALIGN_LEFT, timeline_cursor, 60);
        code_word_table_y += 100;
        timeline_cursor += 30;
    } else {
        panim_scene_add_fade(scene, tree->children.linel,
                             (SDL_Color){ 0xFF, 0, 0, 0xFF },
                             timeline_cursor, 20);
        timeline_cursor += 20;
        add_code_words(scene, tree->children.left,  codeword * 2, codelen + 1);
        panim_scene_add_fade(scene, tree->children.linel,
                             (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF },
                             timeline_cursor, 20);
        panim_scene_add_fade(scene, tree->children.liner,
                             (SDL_Color){ 0xFF, 0, 0, 0xFF },
                             timeline_cursor + 20, 20);
        timeline_cursor += 40;
        add_code_words(scene, tree->children.right, codeword * 2 + 1, codelen + 1);
        panim_scene_add_fade(scene, tree->children.liner,
                             (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF },
                             timeline_cursor, 20);
        timeline_cursor += 20;
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
    CodeTree * huff = build_huff_tree(&scene, "ABRACADABRA");
    timeline_cursor = scene.length_in_frames + 30;
    add_tree_labels(&scene, huff);
    
    timeline_cursor = scene.length_in_frames + 30;
    move_tree(&scene, huff, -250, 0, timeline_cursor, 30);
    
    timeline_cursor = scene.length_in_frames + 30;
    add_code_words(&scene, huff, 0, 0);
    
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