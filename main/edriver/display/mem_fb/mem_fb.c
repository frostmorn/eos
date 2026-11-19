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
  void *vram;
  int32_t width;
  int32_t height;
  uint32_t format;
  eos_clip_rect_t clip;
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

  int32_t width = (int32_t)eos_cfg_get_i(dev->cfg, "width", EOS_DISPLAY_DEFAULT_WIDTH);
  int32_t height = (int32_t)eos_cfg_get_i(dev->cfg, "height", EOS_DISPLAY_DEFAULT_HEIGHT);

  if (width <= 0 || height <= 0) {
    EOS_LOGE("mem_fb: invalid display dimensions %ldx%ld", (long)width, (long)height);
    free(state);
    dev->state = NULL;
    errno = EINVAL;
    return false;
  }

  state->width = width;
  state->height = height;
  state->format = EOS_DISPLAY_FMT_RGB565;
  size_t vram_bytes = (size_t)width * height * eos_display_format_bytes_per_pixel(state->format);
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
  eos_rect_t draw_rect;
  eos_clip_rect_consume(&state->clip, (uint32_t)state->width, (uint32_t)state->height, &draw_rect);

  size_t bytes_per_pixel = eos_display_format_bytes_per_pixel(state->format);
  if ((len % bytes_per_pixel) != 0) {
    errno = EINVAL;
    return -1;
  }
  size_t row_bytes = (size_t)draw_rect.w * bytes_per_pixel;
  if (row_bytes == 0) {
    errno = EINVAL;
    return -1;
  }

  uint32_t total_rows = (uint32_t)(len / row_bytes);
  if (total_rows == 0) {
    errno = EINVAL;
    return -1;
  }
  if (total_rows > draw_rect.h) {
    total_rows = draw_rect.h;
  }

  const uint8_t *src = (const uint8_t *)buf;
  uint8_t *vram_base = (uint8_t *)state->vram;
  for (uint32_t r = 0; r < total_rows; r++) {
    uint32_t row_idx = draw_rect.y + r;
    if (row_idx >= (uint32_t)state->height) break;
    uint8_t *dst_row = vram_base + ((size_t)row_idx * (size_t)state->width + draw_rect.x) * bytes_per_pixel;
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
    uint32_t *w = va_arg(args, uint32_t *);
    if (!w) { errno = EINVAL; return -1; }
    *w = (uint32_t)state->width;
    return 0;
  }
  case EOS_DISPLAY_IOCTL_GET_HEIGHT: {
    uint32_t *h = va_arg(args, uint32_t *);
    if (!h) { errno = EINVAL; return -1; }
    *h = (uint32_t)state->height;
    return 0;
  }
  case EOS_DISPLAY_IOCTL_SET_WINDOW:
    return eos_clip_rect_set(&state->clip, va_arg(args, const eos_display_rect_t *),
                             (uint32_t)state->width, (uint32_t)state->height);
  case EOS_DISPLAY_IOCTL_GET_WINDOW:
    return eos_clip_rect_get(&state->clip, (uint32_t)state->width, (uint32_t)state->height,
                             va_arg(args, eos_display_rect_t *));
  case EOS_DISPLAY_IOCTL_FLUSH:
    return 0;

  case EOS_DISPLAY_IOCTL_GET_FORMATS: {
    uint32_t *fmts = va_arg(args, uint32_t *);
    if (!fmts) { errno = EINVAL; return -1; }
    *fmts = EOS_DISPLAY_FMT_MASK_RGB565 | EOS_DISPLAY_FMT_MASK_RGB888 | EOS_DISPLAY_FMT_MASK_GRAY8;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_GET_FORMAT: {
    uint32_t *fmt = va_arg(args, uint32_t *);
    if (!fmt) { errno = EINVAL; return -1; }
    *fmt = state->format;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_SET_FORMAT: {
    uint32_t fmt = va_arg(args, uint32_t);
    if (fmt == state->format) return 0;
    if (fmt != EOS_DISPLAY_FMT_RGB565 && fmt != EOS_DISPLAY_FMT_RGB888 && fmt != EOS_DISPLAY_FMT_GRAY8) {
      errno = ENOTSUP;
      return -1;
    }
    size_t new_bpp = eos_display_format_bytes_per_pixel(fmt);
    size_t new_bytes = (size_t)state->width * state->height * new_bpp;
    void *new_vram = eos_psram_calloc_prefer(1, new_bytes);
    if (!new_vram) {
      errno = ENOMEM;
      return -1;
    }
    free(state->vram);
    state->vram = new_vram;
    state->format = fmt;
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
