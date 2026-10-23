#include "SDL.h"
#include "ecore/ioctl.h"
#include "ecore/error.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

static int sdl_fb_fd = -1;
static int sdl_width = 0;
static int sdl_height = 0;
static char sdl_error_buf[256] = {0};

static void sdl_query_display_size(void) {
  if (sdl_fb_fd < 0) {
    const char *fb_path = getenv("EOS_FB");
    if (!fb_path) {
      fb_path = "/dev/fb0";
    }
    sdl_fb_fd = open(fb_path, O_RDWR);
  }
  if (sdl_fb_fd >= 0) {
    uint32_t w = 0, h = 0;
    if (ioctl(sdl_fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) == EOS_ERR_NO_ERROR && w > 0) {
      sdl_width = (int)w;
    }
    if (ioctl(sdl_fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) == EOS_ERR_NO_ERROR && h > 0) {
      sdl_height = (int)h;
    }
  }
}

int __attribute__((weak)) SDL_Init(Uint32 flags) {
  (void)flags;
  sdl_query_display_size();
  return 0;
}

void __attribute__((weak)) SDL_Quit(void) {
  if (sdl_fb_fd >= 0) {
    close(sdl_fb_fd);
    sdl_fb_fd = -1;
  }
}

const char * __attribute__((weak)) SDL_GetError(void) {
  return sdl_error_buf;
}

void __attribute__((weak)) SDL_SetError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(sdl_error_buf, sizeof(sdl_error_buf), fmt, args);
  va_end(args);
}

static SDL_Surface *global_screen_surface = NULL;
static SDL_PixelFormat default_pixel_format = {
    .bits_per_pixel = 16,
    .bytes_per_pixel = 2,
    .Rmask = 0xF800,
    .Gmask = 0x07E0,
    .Bmask = 0x001F,
    .Amask = 0x0000
};

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags) {
  (void)bpp;
  (void)flags;
  (void)width;
  (void)height;
  SDL_Init(SDL_INIT_VIDEO);
  sdl_query_display_size();

  if (sdl_width <= 0) sdl_width = 240;
  if (sdl_height <= 0) sdl_height = 280;

  int w = sdl_width;
  int h = sdl_height;

  if (global_screen_surface) {
    if (global_screen_surface->pixels) {
      free(global_screen_surface->pixels);
    }
    free(global_screen_surface);
    global_screen_surface = NULL;
  }

  global_screen_surface = malloc(sizeof(SDL_Surface));
  if (!global_screen_surface) return NULL;
  memset(global_screen_surface, 0, sizeof(SDL_Surface));

  global_screen_surface->w = w;
  global_screen_surface->h = h;
  global_screen_surface->pitch = w * 2;
  global_screen_surface->format = &default_pixel_format;
  global_screen_surface->pixels = malloc(w * h * 2);
  if (!global_screen_surface->pixels) {
    free(global_screen_surface);
    global_screen_surface = NULL;
    return NULL;
  }
  memset(global_screen_surface->pixels, 0, w * h * 2);
  global_screen_surface->refcount = 1;

  return global_screen_surface;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask) {
  (void)flags;
  (void)depth;
  SDL_Surface *surf = malloc(sizeof(SDL_Surface));
  if (!surf) return NULL;
  memset(surf, 0, sizeof(SDL_Surface));

  surf->w = width;
  surf->h = height;
  surf->pitch = width * 2;
  SDL_PixelFormat *fmt = malloc(sizeof(SDL_PixelFormat));
  if (!fmt) {
    free(surf);
    return NULL;
  }
  fmt->bits_per_pixel = 16;
  fmt->bytes_per_pixel = 2;
  fmt->Rmask = Rmask;
  fmt->Gmask = Gmask;
  fmt->Bmask = Bmask;
  fmt->Amask = Amask;
  surf->format = fmt;
  surf->pixels = malloc(width * height * 2);
  if (!surf->pixels) {
    free(fmt);
    free(surf);
    return NULL;
  }
  memset(surf->pixels, 0, width * height * 2);
  surf->refcount = 1;
  return surf;
}

void SDL_FreeSurface(SDL_Surface *surface) {
  if (!surface) return;
  if (surface == global_screen_surface) return; // managed
  if (surface->pixels) free(surface->pixels);
  if (surface->format) free(surface->format);
  free(surface);
}

int SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h) {
  if (!screen || sdl_fb_fd < 0) return -1;
  if (w == 0) w = screen->w;
  if (h == 0) h = screen->h;

  eos_display_rect_t rect = { .x = x, .y = y, .w = w, .h = h };
  ioctl(sdl_fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect);

  size_t size = screen->w * screen->h * 2;
  write(sdl_fb_fd, screen->pixels, size);
  return 0;
}

int SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects) {
  if (!screen || sdl_fb_fd < 0) return -1;
  if (numrects <= 0 || !rects) {
    return SDL_UpdateRect(screen, 0, 0, screen->w, screen->h);
  }
  for (int i = 0; i < numrects; i++) {
    SDL_UpdateRect(screen, rects[i].x, rects[i].y, rects[i].w, rects[i].h);
  }
  return 0;
}

int SDL_Flip(SDL_Surface *screen) {
  if (!screen) return -1;
  return SDL_UpdateRect(screen, 0, 0, screen->w, screen->h);
}

Uint32 __attribute__((weak)) SDL_GetTicks(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (Uint32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void __attribute__((weak)) SDL_Delay(Uint32 ms) {
  usleep(ms * 1000);
}
