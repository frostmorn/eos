#include "SDL2/SDL.h"
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

static int sdl2_fb_fd = -1;
static int sdl2_width = 240;
static int sdl2_height = 280;
static char sdl2_error_buf[256] = {0};

struct SDL_Window {
  int w, h;
  char title[64];
};

struct SDL_Renderer {
  SDL_Window *window;
  uint16_t *pixels;
};

struct SDL_Texture {
  int w, h;
  uint16_t *pixels;
};

int __attribute__((weak)) SDL_Init(Uint32 flags) {
  (void)flags;
  if (sdl2_fb_fd < 0) {
    const char *fb_path = getenv("EOS_FB");
    if (!fb_path) {
      fb_path = "/dev/fb0";
    }
    sdl2_fb_fd = open(fb_path, O_RDWR);
    if (sdl2_fb_fd >= 0) {
      uint32_t w = 240, h = 280;
      if (ioctl(sdl2_fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) == EOS_ERR_NO_ERROR) {
        sdl2_width = (int)w;
      }
      if (ioctl(sdl2_fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) == EOS_ERR_NO_ERROR) {
        sdl2_height = (int)h;
      }
    }
  }
  return 0;
}

void __attribute__((weak)) SDL_Quit(void) {
  if (sdl2_fb_fd >= 0) {
    close(sdl2_fb_fd);
    sdl2_fb_fd = -1;
  }
}

const char * __attribute__((weak)) SDL_GetError(void) {
  return sdl2_error_buf;
}

void __attribute__((weak)) SDL_SetError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(sdl2_error_buf, sizeof(sdl2_error_buf), fmt, args);
  va_end(args);
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
  (void)x;
  (void)y;
  (void)flags;
  SDL_Init(SDL_INIT_VIDEO);
  if (sdl2_fb_fd >= 0) {
    uint32_t dw = 240, dh = 280;
    if (ioctl(sdl2_fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &dw) == EOS_ERR_NO_ERROR) {
      sdl2_width = (int)dw;
    }
    if (ioctl(sdl2_fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &dh) == EOS_ERR_NO_ERROR) {
      sdl2_height = (int)dh;
    }
  }
  if (w <= 0) w = sdl2_width;
  if (h <= 0) h = sdl2_height;
  sdl2_width = w;
  sdl2_height = h;

  SDL_Window *win = malloc(sizeof(SDL_Window));
  if (!win) return NULL;
  win->w = w;
  win->h = h;
  if (title) {
    snprintf(win->title, sizeof(win->title), "%s", title);
  } else {
    win->title[0] = '\0';
  }
  return win;
}

void SDL_DestroyWindow(SDL_Window *window) {
  if (window) free(window);
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags) {
  (void)index;
  (void)flags;
  if (!window) return NULL;
  SDL_Renderer *ren = malloc(sizeof(SDL_Renderer));
  if (!ren) return NULL;
  ren->window = window;
  ren->pixels = malloc(window->w * window->h * 2);
  if (!ren->pixels) {
    free(ren);
    return NULL;
  }
  memset(ren->pixels, 0, window->w * window->h * 2);
  return ren;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer) {
  if (!renderer) return;
  if (renderer->pixels) free(renderer->pixels);
  free(renderer);
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h) {
  (void)renderer;
  (void)format;
  (void)access;
  SDL_Texture *tex = malloc(sizeof(SDL_Texture));
  if (!tex) return NULL;
  tex->w = w;
  tex->h = h;
  tex->pixels = malloc(w * h * 2);
  if (!tex->pixels) {
    free(tex);
    return NULL;
  }
  memset(tex->pixels, 0, w * h * 2);
  return tex;
}

void SDL_DestroyTexture(SDL_Texture *texture) {
  if (!texture) return;
  if (texture->pixels) free(texture->pixels);
  free(texture);
}

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch) {
  if (!texture || !pixels) return -1;
  (void)pitch;
  if (!rect) {
    memcpy(texture->pixels, pixels, texture->w * texture->h * 2);
  } else {
    const uint16_t *src = pixels;
    int src_stride = pitch / 2;
    for (int r = 0; r < rect->h; r++) {
      int dst_y = rect->y + r;
      if (dst_y < 0 || dst_y >= texture->h) continue;
      for (int c = 0; c < rect->w; c++) {
        int dst_x = rect->x + c;
        if (dst_x < 0 || dst_x >= texture->w) continue;
        texture->pixels[dst_y * texture->w + dst_x] = src[r * src_stride + c];
      }
    }
  }
  return 0;
}

int SDL_RenderClear(SDL_Renderer *renderer) {
  if (!renderer || !renderer->pixels) return -1;
  memset(renderer->pixels, 0, renderer->window->w * renderer->window->h * 2);
  return 0;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect) {
  if (!renderer || !texture || !renderer->pixels) return -1;
  (void)srcrect;
  (void)dstrect;
  int copy_w = texture->w < renderer->window->w ? texture->w : renderer->window->w;
  int copy_h = texture->h < renderer->window->h ? texture->h : renderer->window->h;
  for (int y = 0; y < copy_h; y++) {
    memcpy(&renderer->pixels[y * renderer->window->w], &texture->pixels[y * texture->w], copy_w * 2);
  }
  return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer) {
  if (!renderer || sdl2_fb_fd < 0 || !renderer->pixels) return;
  eos_display_rect_t rect = { .x = 0, .y = 0, .w = renderer->window->w, .h = renderer->window->h };
  ioctl(sdl2_fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect);
  write(sdl2_fb_fd, renderer->pixels, renderer->window->w * renderer->window->h * 2);
}

Uint32 __attribute__((weak)) SDL_GetTicks(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (Uint32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void __attribute__((weak)) SDL_Delay(Uint32 ms) {
  usleep(ms * 1000);
}
