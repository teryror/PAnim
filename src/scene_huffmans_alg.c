/*******************************************************************************
Author: Tristan Dannenberg
Notice: No warranty is offered or implied; use this code at your own risk.
*******************************************************************************/

#include "panim.h"

int main(int argc, char *argv[]) {
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