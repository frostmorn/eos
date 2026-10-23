#pragma once
#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_pixels.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Window SDL_Window;

#define SDL_WINDOW_FULLSCREEN 0x00000001
#define SDL_WINDOW_OPENGL     0x00000002
#define SDL_WINDOW_SHOWN      0x00000004

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);

#ifdef __cplusplus
}
#endif
