#pragma once

#include "ecore/input.h"
#include "ecore/ioctl.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static inline void eos_sdl_open_devices(int *fb_fd, int *kbd_fd, int *touch_fd, int *w_out, int *h_out) {
  if (fb_fd && *fb_fd < 0) {
    const char *fb_path = getenv("EOS_FB");
    if (!fb_path) {
      fb_path = (access("/dev/vfb0", F_OK) == 0) ? "/dev/vfb0" : "/dev/fb0";
    }
    *fb_fd = open(fb_path, O_RDWR);
  }
  if (fb_fd && *fb_fd >= 0) {
    uint32_t w = 0, h = 0;
    if (ioctl(*fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) == 0 && w > 0 && w_out) {
      *w_out = (int)w;
    }
    if (ioctl(*fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) == 0 && h > 0 && h_out) {
      *h_out = (int)h;
    }
  }

  if (kbd_fd && *kbd_fd < 0) {
    const char *kbd_path = getenv("EOS_KEYBOARD");
    if (!kbd_path) {
      kbd_path = "/dev/keyboard0";
    }
    *kbd_fd = open(kbd_path, O_RDONLY);
  }

  if (touch_fd && *touch_fd < 0) {
    const char *touch_path = getenv("EOS_TOUCH");
    if (!touch_path) {
      touch_path = "/dev/touch0";
    }
    *touch_fd = open(touch_path, O_RDONLY);
    if (*touch_fd < 0 && strcmp(touch_path, "/dev/touch0") == 0) {
      *touch_fd = open("/dev/touch", O_RDONLY);
    }
  }
}

static inline uint32_t eos_sdl_map_key(uint16_t code, int is_sdl2) {
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

typedef enum {
  EOS_SDL_TOUCH_NONE = 0,
  EOS_SDL_TOUCH_DOWN,
  EOS_SDL_TOUCH_MOTION,
  EOS_SDL_TOUCH_UP
} eos_sdl_touch_kind_t;

static inline eos_sdl_touch_kind_t eos_sdl_process_touch(int touch_fd, bool *touch_pressed,
                                                         uint16_t *last_x, uint16_t *last_y,
                                                         uint16_t *out_x, uint16_t *out_y,
                                                         int16_t *out_dx, int16_t *out_dy) {
  if (touch_fd < 0) return EOS_SDL_TOUCH_NONE;
  eos_input_event_t ev;
  int ret = ioctl(touch_fd, EOS_TOUCH_IOCTL_POLL, &ev);
  if (ret <= 0 || ev.type != EOS_INPUT_EVENT_TOUCH) return EOS_SDL_TOUCH_NONE;

  *out_x = ev.x;
  *out_y = ev.y;
  if (ev.state) {
    if (!*touch_pressed) {
      *touch_pressed = true;
      *last_x = ev.x;
      *last_y = ev.y;
      return EOS_SDL_TOUCH_DOWN;
    }
    if (ev.x != *last_x || ev.y != *last_y) {
      if (out_dx) *out_dx = (int16_t)(ev.x - *last_x);
      if (out_dy) *out_dy = (int16_t)(ev.y - *last_y);
      *last_x = ev.x;
      *last_y = ev.y;
      return EOS_SDL_TOUCH_MOTION;
    }
  } else if (*touch_pressed) {
    *touch_pressed = false;
    return EOS_SDL_TOUCH_UP;
  }
  return EOS_SDL_TOUCH_NONE;
}

static inline void eos_sdl_get_mouse_coords(int *x, int *y, uint16_t last_x, uint16_t last_y) {
  if (x) *x = (int)last_x;
  if (y) *y = (int)last_y;
}
