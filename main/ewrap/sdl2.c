#include "SDL2/SDL.h"
#include "ecore/ioctl.h"
#include "ecore/input.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

typedef struct {
  int fb_fd;
  int keyboard_fd;
  int width;
  int height;
  char error_buf[256];
} sdl2_context_t;

static sdl2_context_t *sdl2_ctx = NULL;

static sdl2_context_t *sdl2_get_context(void) {
  if (!sdl2_ctx) {
    sdl2_ctx = calloc(1, sizeof(sdl2_context_t));
    if (sdl2_ctx) {
      sdl2_ctx->fb_fd = -1;
      sdl2_ctx->keyboard_fd = -1;
    }
  }
  return sdl2_ctx;
}

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

static void sdl2_query_display_and_input(sdl2_context_t *ctx) {
  if (!ctx) return;
  if (ctx->fb_fd < 0) {
    const char *fb_path = getenv("EOS_FB");
    if (!fb_path) {
      fb_path = "/dev/fb0";
    }
    ctx->fb_fd = open(fb_path, O_RDWR);
  }
  if (ctx->fb_fd >= 0) {
    uint32_t w = 0, h = 0;
    if (ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) == 0 && w > 0) {
      ctx->width = (int)w;
    }
    if (ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) == 0 && h > 0) {
      ctx->height = (int)h;
    }
  }

  if (ctx->keyboard_fd < 0) {
    const char *kbd_path = getenv("EOS_KEYBOARD");
    if (!kbd_path) {
      kbd_path = "/dev/keyboard0";
    }
    ctx->keyboard_fd = open(kbd_path, O_RDONLY);
  }
}

int __attribute__((weak)) SDL_Init(Uint32 flags) {
  (void)flags;
  sdl2_context_t *ctx = sdl2_get_context();
  if (ctx) {
    sdl2_query_display_and_input(ctx);
  }
  return 0;
}

void __attribute__((weak)) SDL_Quit(void) {
  if (sdl2_ctx) {
    if (sdl2_ctx->fb_fd >= 0) {
      close(sdl2_ctx->fb_fd);
    }
    if (sdl2_ctx->keyboard_fd >= 0) {
      close(sdl2_ctx->keyboard_fd);
    }
    free(sdl2_ctx);
    sdl2_ctx = NULL;
  }
}

const char * __attribute__((weak)) SDL_GetError(void) {
  sdl2_context_t *ctx = sdl2_get_context();
  return ctx ? ctx->error_buf : "";
}

void __attribute__((weak)) SDL_SetError(const char *fmt, ...) {
  sdl2_context_t *ctx = sdl2_get_context();
  if (!ctx) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(ctx->error_buf, sizeof(ctx->error_buf), fmt, args);
  va_end(args);
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
  (void)x;
  (void)y;
  (void)flags;
  (void)w;
  (void)h;
  SDL_Init(SDL_INIT_VIDEO);
  sdl2_context_t *ctx = sdl2_get_context();
  if (!ctx) return NULL;
  sdl2_query_display_and_input(ctx);

  if (ctx->width <= 0) ctx->width = 240;
  if (ctx->height <= 0) ctx->height = 280;

  int window_w = ctx->width;
  int window_h = ctx->height;

  SDL_Window *win = malloc(sizeof(SDL_Window));
  if (!win) return NULL;
  win->w = window_w;
  win->h = window_h;
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
  sdl2_context_t *ctx = sdl2_get_context();
  if (w <= 0 || h <= 0) {
    if (ctx) sdl2_query_display_and_input(ctx);
    w = (ctx && ctx->width > 0) ? ctx->width : 240;
    h = (ctx && ctx->height > 0) ? ctx->height : 280;
  }
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

int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h) {
  if (!texture) return -1;
  if (format) *format = SDL_PIXELFORMAT_RGB565;
  if (access) *access = SDL_TEXTUREACCESS_STATIC;
  if (w) *w = texture->w;
  if (h) *h = texture->h;
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
  sdl2_context_t *ctx = sdl2_get_context();
  if (!renderer || !ctx || ctx->fb_fd < 0 || !renderer->pixels) return;
  eos_display_rect_t rect = { .x = 0, .y = 0, .w = renderer->window->w, .h = renderer->window->h };
  ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect);
  write(ctx->fb_fd, renderer->pixels, renderer->window->w * renderer->window->h * 2);
}

Uint32 __attribute__((weak)) SDL_GetTicks(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (Uint32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void __attribute__((weak)) SDL_Delay(Uint32 ms) {
  usleep(ms * 1000);
}

void __attribute__((weak)) SDL_PumpEvents(void) {
  // no-op
}

static Uint32 eos_to_sdl2_key(uint16_t code) {
  switch (code) {
    case KEY_UP: return 1073741906;
    case KEY_DOWN: return 1073741905;
    case KEY_LEFT: return 1073741904;
    case KEY_RIGHT: return 1073741903;
    case KEY_ENTER: return 13;
    case KEY_SPACE: return 32;
    case KEY_A: return 97;
    case KEY_B: return 98;
    case KEY_C: return 99;
    case KEY_D: return 100;
    case KEY_ESC: return 27;
    default: return 0;
  }
}

int __attribute__((weak)) SDL_PollEvent(SDL_Event *event) {
  if (!event) return 0;
  sdl2_context_t *ctx = sdl2_get_context();
  if (!ctx) return 0;
  sdl2_query_display_and_input(ctx);
  if (ctx->keyboard_fd < 0) return 0;

  eos_input_event_t ev;
  int ret = ioctl(ctx->keyboard_fd, EOS_KEYBOARD_IOCTL_POLL, &ev);
  if (ret > 0 && ev.type == EOS_INPUT_EVENT_KEYBOARD) {
    event->type = SDL_KEYDOWN;
    event->key.state = 1;
    event->key.keysym.sym = eos_to_sdl2_key(ev.code);
    event->key.keysym.scancode = event->key.keysym.sym;
    return 1;
  }
  return 0;
}

int __attribute__((weak)) SDL_WaitEvent(SDL_Event *event) {
  while (1) {
    if (SDL_PollEvent(event)) {
      return 1;
    }
    usleep(10000);
  }
}
