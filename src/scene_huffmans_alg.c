/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "panim.h"
#include "prefix_coding.h"

// -----------------------

static size_t timeline_cursor = 10;

static CodeTree *
build_huff_tree(PAnimScene * scene, char * message) {
    // Count symbol frequencies
    int freqs[256] = {0};
    while (*message) freqs[*message++]++;
    
    // Forest of binary trees, will eventually be combined into a single tree
    CodeTree *forest = NULL;
    for (int i = 0; i < 256; ++i) {
        if (freqs[i] > 0) {
            CodeTree new_node = make_leaf_node(
                scene, (char)i, freqs[i],
                340 + 100 * ((int)buf_len(forest) + 1), 100,
                timeline_cursor);
            buf_push(forest, new_node);
            
            timeline_cursor += 15;
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
        
        CodeTree * left  = (CodeTree *) malloc(sizeof(CodeTree));
        CodeTree * right = (CodeTree *) malloc(sizeof(CodeTree));
        
        int write_idx;
        if (min1 < min2) {
            *left  = forest[min1];
            *right = forest[min2];
            write_idx = min1;
            
            for (int i = min2 + 1; i < buf_len(forest); ++i) {
                forest[i - 1] = forest[i];
            }
            buf__hdr(forest)->len -= 1;
        } else {
            *left  = forest[min2];
            *right = forest[min1];
            write_idx = min2;
            
            for (int i = min1 + 1; i < buf_len(forest); ++i) {
                forest[i - 1] = forest[i];
            }
            buf__hdr(forest)->len -= 1;
        }
        
        CodeTree new_node = combine_nodes(scene, left, right, timeline_cursor);
        forest[write_idx] = new_node;
        
        timeline_cursor += 135;
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
        char * code = (char *) calloc(codelen + 4, 1);
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
    load_content(pnm.renderer);
    
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