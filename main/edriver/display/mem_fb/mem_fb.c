#include "includes.h"
#ifdef EOS_DRV_DISPLAY_MEM_FB_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint16_t *vram;
  int32_t width;
  int32_t height;
  uint32_t clip_x;
  uint32_t clip_y;
  uint32_t clip_w;
  uint32_t clip_h;
  bool has_clip;
} mem_fb_state_t;

bool driver_display_mem_fb_init(eos_dev_t *dev) {
  if (!dev) {
    errno = EINVAL;
    return false;
  }

  mem_fb_state_t *state = eos_psram_calloc_prefer(1, sizeof(mem_fb_state_t));
  if (!state) {
    errno = ENOMEM;
    return false;
  }
  dev->state = state;

  int32_t width = (int32_t)eos_cfg_get_i(dev->cfg, "width", 280);
  int32_t height = (int32_t)eos_cfg_get_i(dev->cfg, "height", 240);

  if (width <= 0 || height <= 0) {
    EOS_LOGE("mem_fb: invalid display dimensions %ldx%ld", (long)width, (long)height);
    free(state);
    dev->state = NULL;
    errno = EINVAL;
    return false;
  }

  state->width = width;
  state->height = height;
  size_t vram_bytes = (size_t)width * height * sizeof(uint16_t);
  state->vram = eos_psram_calloc_prefer(1, vram_bytes);
  if (!state->vram) {
    EOS_LOGE("mem_fb: failed to allocate %zu bytes for VRAM", vram_bytes);
    free(state);
    dev->state = NULL;
    errno = ENOMEM;
    return false;
  }

  EOS_LOGI("mem_fb: initialized %ldx%ld in-memory display (%zu bytes)",
           (long)width, (long)height, vram_bytes);
  return true;
}

ssize_t driver_display_mem_fb_write(eos_dev_t *dev, int fd, const void *buf, size_t len) {
  (void)fd;
  if (!dev || !dev->state || !buf || len == 0) {
    errno = EINVAL;
    return -1;
  }

  mem_fb_state_t *state = dev->state;
  uint32_t x = state->has_clip ? state->clip_x : 0;
  uint32_t y = state->has_clip ? state->clip_y : 0;
  uint32_t w = state->has_clip ? state->clip_w : (uint32_t)state->width;
  uint32_t h = state->has_clip ? state->clip_h : (uint32_t)state->height;

  size_t bytes_per_pixel = sizeof(uint16_t);
  size_t row_bytes = (size_t)w * bytes_per_pixel;
  if (row_bytes == 0) {
    errno = EINVAL;
    return -1;
  }

  uint32_t total_rows = (uint32_t)(len / row_bytes);
  if (total_rows == 0) {
    errno = EINVAL;
    return -1;
  }
  if (total_rows > h) {
    total_rows = h;
  }

  const uint8_t *src = (const uint8_t *)buf;
  for (uint32_t r = 0; r < total_rows; r++) {
    uint32_t row_idx = y + r;
    if (row_idx >= (uint32_t)state->height) break;
    uint16_t *dst_row = state->vram + row_idx * state->width + x;
    memcpy(dst_row, src + r * row_bytes, row_bytes);
  }

  return (ssize_t)(total_rows * row_bytes);
}

int driver_display_mem_fb_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }

  mem_fb_state_t *state = dev->state;
  switch (cmd) {
  case EOS_DISPLAY_IOCTL_GET_WIDTH: {
    uint32_t *width = va_arg(args, uint32_t *);
    if (!width) {
      errno = EINVAL;
      return -1;
    }
    *width = (uint32_t)state->width;
    return 0;
  }
  case EOS_DISPLAY_IOCTL_GET_HEIGHT: {
    uint32_t *height = va_arg(args, uint32_t *);
    if (!height) {
      errno = EINVAL;
      return -1;
    }
    *height = (uint32_t)state->height;
    return 0;
  }
  case EOS_DISPLAY_IOCTL_SET_WINDOW: {
    const eos_display_rect_t *rect = va_arg(args, const eos_display_rect_t *);
    if (rect) {
      if (rect->w == 0 || rect->h == 0 ||
          rect->x >= (uint32_t)state->width ||
          rect->y >= (uint32_t)state->height ||
          rect->x + rect->w > (uint32_t)state->width ||
          rect->y + rect->h > (uint32_t)state->height) {
        state->has_clip = false;
        errno = EINVAL;
        return -1;
      }
      state->clip_x = rect->x;
      state->clip_y = rect->y;
      state->clip_w = rect->w;
      state->clip_h = rect->h;
      state->has_clip = true;
    } else {
      state->has_clip = false;
    }
    return 0;
  }
  case EOS_DISPLAY_IOCTL_GET_WINDOW: {
    eos_display_rect_t *rect = va_arg(args, eos_display_rect_t *);
    if (!rect) {
      errno = EINVAL;
      return -1;
    }
    if (state->has_clip) {
      rect->x = state->clip_x;
      rect->y = state->clip_y;
      rect->w = state->clip_w;
      rect->h = state->clip_h;
    } else {
      rect->x = 0;
      rect->y = 0;
      rect->w = (uint32_t)state->width;
      rect->h = (uint32_t)state->height;
    }
    return 0;
  }
  case EOS_DISPLAY_IOCTL_FLUSH: {
    return 0;
  }
  default:
    errno = ENOTTY;
    return -1;
  }
}

void driver_display_mem_fb_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  mem_fb_state_t *state = dev->state;
  if (state->vram) {
    free(state->vram);
  }
  free(state);
  dev->state = NULL;
}

EOS_DRV_ATTR eos_drv_t driver_display_mem_fb = {
    EOS_DRV_INIT,
    .scope = "display",
    .name = "mem_fb",
    .devname = "fb",
    .init = driver_display_mem_fb_init,
    .write = driver_display_mem_fb_write,
    .ioctl = driver_display_mem_fb_ioctl,
    .shutdown = driver_display_mem_fb_shutdown};

#endif
