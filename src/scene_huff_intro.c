/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "panim.h"
#include "prefix_coding.h"

int main(int argc, char *argv[]) {
    PAnimScene scene = {0};
    scene.screen_width  = 1280;
    scene.screen_height =  720;
    scene.bg_color = (SDL_Color){ 32, 32, 32, 0xFF };
    
    PAnimEngine pnm = panim_engine_begin_preview(&scene);
    load_content(pnm.renderer);
    
    // TODO: Populate scene
    
    return panim_main(argc, argv, &pnm, &scene);
}