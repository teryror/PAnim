/***********************************************************
 Rendering Test with SDL2, SDL_image and SDL_ttf
 
 The goal is to open a window, handle input, draw an image,
 and finally, some text.
 
 To build:
     cl sdl_test.c /Febin\sdl_test.exe /Iinclude /link /libpath:lib\x64 SDL2.lib SDL2main.lib SDL2_image.lib SDL2_ttf.lib
***********************************************************/

#include "stdio.h"
#include "SDL/SDL.h"
#include "SDL/SDL_image.h"
#include "SDL/SDL_ttf.h"

// SDL is supposed to have its own entry point, and defines main as SDL_main.
// for some reason, I get a linker error for a missing entry point, though,
// and SDL seems to work fine even without all this nonsense
#undef main

#define ERROR(E) do { fprintf(stderr, "Error: " E "\n"); exit(1); } while (0)
#define WinWidth 1280
#define WinHeight 720

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) ERROR("initialization failed (SDL)!");
    if (TTF_Init() != 0) ERROR("initialization failed (TTF)!");
    
    SDL_Window *window = SDL_CreateWindow(
        "Hello, SDL!",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WinWidth, WinHeight, 0);
    if (window == NULL) ERROR("failed to create window!");
    
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) ERROR("failed to create renderer!");
    
    // path is relative to working directory, _not_ the executable file!
    SDL_Texture *image = IMG_LoadTexture(renderer, "test.png");
    SDL_Texture *text  = NULL;
    
    { // Rendering Text
        TTF_Font *font = TTF_OpenFont("Oswald-Bold.ttf", 22);
        SDL_Surface *surf = TTF_RenderText_Solid(
            font, "Hello, SDL!", (SDL_Color){ 255, 255, 255, 255 });
        text = SDL_CreateTextureFromSurface(renderer, surf);
        
        SDL_FreeSurface(surf);
        TTF_CloseFont(font);
    }
    
    int w, h;
    SDL_QueryTexture(image, NULL, NULL, &w, &h);
    SDL_Rect dst_rect_img = (SDL_Rect){ WinWidth/2 - w/2, WinHeight/2 - h/2, w, h };
    
    SDL_QueryTexture(text, NULL, NULL, &w, &h);
    SDL_Rect dst_rect_txt = (SDL_Rect){ WinWidth/2 - w/2, dst_rect_img.y - h*2, w, h };
    
    while (1) {
        SDL_Event e;
        if (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
            else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_ESCAPE) break;
        }
        
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, text,  NULL, &dst_rect_txt);
        SDL_RenderCopy(renderer, image, NULL, &dst_rect_img);
        SDL_RenderPresent(renderer);
        
        SDL_Delay(16); // poor man's vsync :(
    }
    
    {
        // We can in principle do without this, since the OS will clean up anyway,
        // but this is good practice in any other context.
        SDL_DestroyTexture(image);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    
    return 0;
}