#pragma once
#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_SWSURFACE 0x00000000
#define SDL_HWSURFACE 0x00000001
#define SDL_FULLSCREEN 0x00008000

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_Flip(SDL_Surface *screen);
int SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
int SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);

#ifdef __cplusplus
}
#endif
