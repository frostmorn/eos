#include "SDL.h"
#include "ecore/ioctl.h"
#include "ecore/error.h"
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
  SDL_Surface *screen_surface;
  SDL_PixelFormat default_pixel_format;
} sdl_context_t;

static sdl_context_t *sdl_ctx = NULL;

static sdl_context_t *sdl_get_context(void) {
  if (!sdl_ctx) {
    sdl_ctx = calloc(1, sizeof(sdl_context_t));
    if (sdl_ctx) {
      sdl_ctx->fb_fd = -1;
      sdl_ctx->keyboard_fd = -1;
      sdl_ctx->default_pixel_format.bits_per_pixel = 16;
      sdl_ctx->default_pixel_format.bytes_per_pixel = 2;
      sdl_ctx->default_pixel_format.Rmask = 0xF800;
      sdl_ctx->default_pixel_format.Gmask = 0x07E0;
      sdl_ctx->default_pixel_format.Bmask = 0x001F;
      sdl_ctx->default_pixel_format.Amask = 0x0000;
    }
  }
  return sdl_ctx;
}

static void sdl_query_display_and_input(sdl_context_t *ctx) {
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
    if (ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) == EOS_ERR_NO_ERROR && w > 0) {
      ctx->width = (int)w;
    }
    if (ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) == EOS_ERR_NO_ERROR && h > 0) {
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
  sdl_context_t *ctx = sdl_get_context();
  if (ctx) {
    sdl_query_display_and_input(ctx);
  }
  return 0;
}

void __attribute__((weak)) SDL_Quit(void) {
  if (sdl_ctx) {
    if (sdl_ctx->fb_fd >= 0) {
      close(sdl_ctx->fb_fd);
    }
    if (sdl_ctx->keyboard_fd >= 0) {
      close(sdl_ctx->keyboard_fd);
    }
    if (sdl_ctx->screen_surface) {
      if (sdl_ctx->screen_surface->pixels) {
        free(sdl_ctx->screen_surface->pixels);
      }
      free(sdl_ctx->screen_surface);
    }
    free(sdl_ctx);
    sdl_ctx = NULL;
  }
}

const char * __attribute__((weak)) SDL_GetError(void) {
  sdl_context_t *ctx = sdl_get_context();
  return ctx ? ctx->error_buf : "";
}

void __attribute__((weak)) SDL_SetError(const char *fmt, ...) {
  sdl_context_t *ctx = sdl_get_context();
  if (!ctx) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(ctx->error_buf, sizeof(ctx->error_buf), fmt, args);
  va_end(args);
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags) {
  (void)bpp;
  (void)flags;
  (void)width;
  (void)height;
  SDL_Init(SDL_INIT_VIDEO);
  sdl_context_t *ctx = sdl_get_context();
  if (!ctx) return NULL;
  sdl_query_display_and_input(ctx);

  if (ctx->width <= 0) ctx->width = 240;
  if (ctx->height <= 0) ctx->height = 280;

  int w = ctx->width;
  int h = ctx->height;

  if (ctx->screen_surface) {
    if (ctx->screen_surface->pixels) {
      free(ctx->screen_surface->pixels);
    }
    free(ctx->screen_surface);
    ctx->screen_surface = NULL;
  }

  ctx->screen_surface = malloc(sizeof(SDL_Surface));
  if (!ctx->screen_surface) return NULL;
  memset(ctx->screen_surface, 0, sizeof(SDL_Surface));

  ctx->screen_surface->w = w;
  ctx->screen_surface->h = h;
  ctx->screen_surface->pitch = w * 2;
  ctx->screen_surface->format = &ctx->default_pixel_format;
  ctx->screen_surface->pixels = malloc(w * h * 2);
  if (!ctx->screen_surface->pixels) {
    free(ctx->screen_surface);
    ctx->screen_surface = NULL;
    return NULL;
  }
  memset(ctx->screen_surface->pixels, 0, w * h * 2);
  ctx->screen_surface->refcount = 1;

  return ctx->screen_surface;
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
  sdl_context_t *ctx = sdl_get_context();
  if (ctx && surface == ctx->screen_surface) return; // managed
  if (surface->pixels) free(surface->pixels);
  if (surface->format) free(surface->format);
  free(surface);
}

int SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h) {
  sdl_context_t *ctx = sdl_get_context();
  if (!screen || !ctx || ctx->fb_fd < 0) return -1;
  if (w == 0) w = screen->w;
  if (h == 0) h = screen->h;

  eos_display_rect_t rect = { .x = x, .y = y, .w = w, .h = h };
  ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect);

  size_t size = screen->w * screen->h * 2;
  write(ctx->fb_fd, screen->pixels, size);
  return 0;
}

int SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects) {
  if (!screen) return -1;
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

void __attribute__((weak)) SDL_PumpEvents(void) {
  // no-op
}

static Uint16 eos_to_sdl_key(uint16_t code, int is_sdl2) {
  switch (code) {
    case KEY_UP: return is_sdl2 ? 1073741906 : 273;
    case KEY_DOWN: return is_sdl2 ? 1073741905 : 274;
    case KEY_LEFT: return is_sdl2 ? 1073741904 : 276;
    case KEY_RIGHT: return is_sdl2 ? 1073741903 : 275;
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
  sdl_context_t *ctx = sdl_get_context();
  if (!ctx) return 0;
  sdl_query_display_and_input(ctx);
  if (ctx->keyboard_fd < 0) return 0;

  eos_input_event_t ev;
  int ret = ioctl(ctx->keyboard_fd, EOS_KEYBOARD_IOCTL_POLL, &ev);
  if (ret > 0 && ev.type == EOS_INPUT_EVENT_KEYBOARD) {
    event->type = SDL_KEYDOWN;
    event->key.state = 1;
    event->key.keysym.sym = eos_to_sdl_key(ev.code, 0);
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
