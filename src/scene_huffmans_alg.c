/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "panim.h"
#include "assert.h"

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

// -----------------------

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

int main(int argc, char *argv[]) {
    CodeTree * root = build_huff_tree("ABRACADABRA");
    
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