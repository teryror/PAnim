/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

static SDL_Texture * circle;
static TTF_Font    * font;

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

static CodeTree
make_leaf_node(PAnimScene * scene,
               char symbol, int freq,
               int center_x, int center_y,
               size_t begin_frame)
{
    CodeTree result;
    result.type = CTT_LEAF;
    result.freq = freq;
    result.sym.symbol  = symbol;
    
    result.sym.bgi = panim_fade_in_image(
        scene, circle, 1, center_x, center_y,
        begin_frame, 30);
    
    char *lbl = (char *) malloc(2);
    lbl[0] = symbol; lbl[1] = 0;
    result.sym.txt = panim_fade_in_text(
        scene, lbl, font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF }, 2,
        center_x, center_y, PNM_TXT_ALIGN_CENTER, begin_frame, 30);
    
    lbl = (char *) malloc(2);
    snprintf(lbl, 2, "%d", freq);
    result.sym.cnt = panim_fade_in_text(
        scene, lbl, font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF }, 2,
        center_x, center_y + 70, PNM_TXT_ALIGN_CENTER, begin_frame, 30);
    
    return result;
}

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

static CodeTree
combine_nodes(PAnimScene * scene,
              CodeTree * left,
              CodeTree * right,
              size_t begin_frame)
{
    CodeTree result = {0};
    result.type = CTT_INTERNAL;
    result.freq = left->freq + right->freq;
    result.children.left  = left;
    result.children.right = right;
    
    move_tree(scene, left,  0, 100, begin_frame, 30);
    move_tree(scene, right, 0, 100, begin_frame, 30);
    begin_frame += 30;
    
    int xl = (left->type == CTT_LEAF)
        ? left->sym.txt->txt.center_x
        : left->children.node_txt->txt.center_x;
    int xr = (right->type == CTT_LEAF)
        ? right->sym.txt->txt.center_x
        : right->children.node_txt->txt.center_x;
    
    result.children.node_bg = panim_fade_in_image(
        scene, circle, 1, (xl + xr) / 2, 100, begin_frame, 60);
    
    char * lbl = (char *) malloc(4);
    snprintf(lbl, 4, "%d", result.freq);
    result.children.node_txt = panim_fade_in_text(
        scene, lbl, font, (SDL_Color){ 0xFF, 0xFF, 0xFF, 0xFF }, 2,
        (xl + xr) / 2, 100, PNM_TXT_ALIGN_CENTER, begin_frame, 60);
    begin_frame += 45;
        
    SDL_Color line_color = (SDL_Color){ 0xC8, 0xC8, 0xC8, 0xFF };
    result.children.linel = panim_draw_line(
        scene, line_color, 0, (xl + xr) / 2, 100, xl, 200, begin_frame, 60);
    result.children.liner = panim_draw_line(
        scene, line_color, 0, (xl + xr) / 2, 100, xr, 200, begin_frame, 60);
        
    return result;
}
    
static void
load_content(SDL_Renderer * renderer) {
    circle = IMG_LoadTexture(renderer, "circle.png");
    font   = TTF_OpenFont("bin/Oswald-Bold.ttf", 36);
}