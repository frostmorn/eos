#pragma once
#include "SDL_stdinc.h"
#include "SDL_rect.h"

typedef struct SDL_PixelFormat {
  Uint8 bits_per_pixel;
  Uint8 bytes_per_pixel;
  Uint32 Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_Surface {
  Uint32 flags;
  SDL_PixelFormat *format;
  int w, h;
  int pitch;
  void *pixels;
  int userdata;
  SDL_Rect clip_rect;
  int refcount;
} SDL_Surface;
