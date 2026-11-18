#include "includes.h"
#ifdef EOS_DRV_DISPLAY_QEMU_RGB_ENABLED

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_qemu_rgb.h>
#include <esp_memory_utils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "emisc/psram_alloc.h"

// ── State ─────────────────────────────────────────────────────

typedef struct {
  esp_lcd_panel_handle_t panel;
  void *vram;
  int32_t width;
  int32_t height;
  uint8_t rotation;
  uint8_t bpp;
  eos_clip_rect_t clip;
  SemaphoreHandle_t lock;
} qemu_rgb_state_t;

// ── Implementation ────────────────────────────────────────────

bool driver_display_qemu_rgb_init(eos_dev_t *dev) {
  if (!dev) {
    errno = EINVAL;
    return false;
  }

  qemu_rgb_state_t *state = eos_psram_calloc_prefer(1, sizeof(qemu_rgb_state_t));
  if (!state) {
    errno = ENOMEM;
    return false;
  }
  dev->state = state;

  int32_t width = (int32_t)eos_cfg_get_i(dev->cfg, "width", EOS_QEMU_DISPLAY_WIDTH);
  int32_t height = (int32_t)eos_cfg_get_i(dev->cfg, "height", EOS_QEMU_DISPLAY_HEIGHT);
  int32_t bpp = (int32_t)eos_cfg_get_i(dev->cfg, "bpp", 16);
  int32_t rotation = (int32_t)eos_cfg_get_i(dev->cfg, "rotation", 0);

  if (width <= 0 || height <= 0) {
    EOS_LOGE("qemu_rgb: invalid display dimensions %ldx%ld", (long)width, (long)height);
    free(state);
    dev->state = NULL;
    errno = EINVAL;
    return false;
  }

  esp_lcd_rgb_qemu_config_t panel_cfg = {
      .width = (uint16_t)width,
      .height = (uint16_t)height,
      .bpp = (bpp == 32) ? RGB_QEMU_BPP_32 : RGB_QEMU_BPP_16,
  };

  esp_err_t err = esp_lcd_new_rgb_qemu(&panel_cfg, &state->panel);
  if (err != ESP_OK) {
    EOS_LOGE("qemu_rgb: esp_lcd_new_rgb_qemu failed: %s", esp_err_to_name(err));
    free(state);
    dev->state = NULL;
    errno = EIO;
    return false;
  }

  err = esp_lcd_panel_reset(state->panel);
  if (err != ESP_OK) {
    EOS_LOGW("qemu_rgb: panel reset warning: %s", esp_err_to_name(err));
  }

  err = esp_lcd_panel_init(state->panel);
  if (err != ESP_OK) {
    EOS_LOGE("qemu_rgb: panel init failed: %s", esp_err_to_name(err));
    esp_lcd_panel_del(state->panel);
    free(state);
    dev->state = NULL;
    errno = EIO;
    return false;
  }

  err = esp_lcd_panel_disp_on_off(state->panel, true);
  if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
    EOS_LOGW("qemu_rgb: display ON warning: %s", esp_err_to_name(err));
  }

  void *fb = NULL;
  err = esp_lcd_rgb_qemu_get_frame_buffer(state->panel, &fb);
  if (err == ESP_OK && fb) {
    state->vram = fb;
  }

  state->width = width;
  state->height = height;
  state->bpp = (bpp == 32) ? 32 : 16;
  state->rotation = (uint8_t)(rotation & 3);
  state->lock = xSemaphoreCreateRecursiveMutex();

  // Apply rotation if requested
  if (state->rotation != 0) {
    switch (state->rotation) {
    case 1:
      esp_lcd_panel_swap_xy(state->panel, true);
      esp_lcd_panel_mirror(state->panel, true, false);
      state->width = height;
      state->height = width;
      break;
    case 2:
      esp_lcd_panel_swap_xy(state->panel, false);
      esp_lcd_panel_mirror(state->panel, true, true);
      break;
    case 3:
      esp_lcd_panel_swap_xy(state->panel, true);
      esp_lcd_panel_mirror(state->panel, false, true);
      state->width = height;
      state->height = width;
      break;
    default:
      break;
    }
  }

  EOS_LOGI("qemu_rgb: virtual RGB panel initialized (%ldx%ld, %d bpp)",
           (long)state->width, (long)state->height, (int)state->bpp);
  return true;
}

ssize_t driver_display_qemu_rgb_write(eos_dev_t *dev, int fd, const void *buf, size_t len) {
  (void)fd;
  if (!dev || !dev->state || !buf || len == 0) {
    errno = EINVAL;
    return -1;
  }

  qemu_rgb_state_t *state = dev->state;
  if (state->lock) {
    xSemaphoreTakeRecursive(state->lock, portMAX_DELAY);
  }

  eos_rect_t r;
  eos_clip_rect_consume(&state->clip, (uint32_t)state->width, (uint32_t)state->height, &r);

  uint8_t bytes_per_pixel = (state->bpp == RGB_QEMU_BPP_32) ? 4 : 2;
  uint32_t total_rows = eos_rect_calc_rows(len, r.w, bytes_per_pixel, r.h);
  if (total_rows == 0) {
    if (state->lock) xSemaphoreGiveRecursive(state->lock);
    errno = EINVAL;
    return -1;
  }
  size_t row_bytes = (size_t)r.w * bytes_per_pixel;
  size_t draw_bytes = (size_t)total_rows * row_bytes;
  const void *draw_buf = buf;

  // QEMU's virtual RGB hardware controller verifies that pixel data resides in
  // either dedicated VRAM (0x20000000..0x201d4c00) or internal SRAM (0x3fc80000..).
  // If the caller passes a buffer located in external PSRAM/SPIRAM, copy through
  // dedicated VRAM so the DMA access check in QEMU's rgb_update succeeds.
  if (state->vram && buf != state->vram && !esp_ptr_internal(buf)) {
    if (draw_bytes <= EOS_QEMU_VRAM_MAX_BYTES) {
      memcpy(state->vram, buf, draw_bytes);
      draw_buf = state->vram;
    }
  }

  esp_err_t err = esp_lcd_panel_draw_bitmap(state->panel, (int)r.x, (int)r.y,
                                            (int)(r.x + r.w), (int)(r.y + total_rows), draw_buf);
  if (err != ESP_OK) {
    EOS_LOGE("qemu_rgb: draw_bitmap failed: %s", esp_err_to_name(err));
    if (state->lock) xSemaphoreGiveRecursive(state->lock);
    errno = EIO;
    return -1;
  }

  if (state->lock) xSemaphoreGiveRecursive(state->lock);
  return (ssize_t)draw_bytes;
}

int driver_display_qemu_rgb_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  qemu_rgb_state_t *state = dev->state;
  if (!state) {
    errno = ENODEV;
    return -1;
  }

  int ret = 0;
  switch (cmd) {
  case EOS_DISPLAY_IOCTL_GET_WIDTH: {
    uint32_t *width = va_arg(args, uint32_t *);
    if (!width) {
      errno = EINVAL;
      return -1;
    }
    *width = (uint32_t)state->width;
    break;
  }
  case EOS_DISPLAY_IOCTL_GET_HEIGHT: {
    uint32_t *height = va_arg(args, uint32_t *);
    if (!height) {
      errno = EINVAL;
      return -1;
    }
    *height = (uint32_t)state->height;
    break;
  }
  case EOS_DISPLAY_IOCTL_SET_WINDOW:
    ret = eos_clip_rect_set(&state->clip, va_arg(args, const eos_display_rect_t *),
                            (uint32_t)state->width, (uint32_t)state->height);
    break;
  case EOS_DISPLAY_IOCTL_GET_WINDOW:
    ret = eos_clip_rect_get(&state->clip, (uint32_t)state->width, (uint32_t)state->height,
                            va_arg(args, eos_display_rect_t *));
    break;
  case EOS_DISPLAY_IOCTL_LOCK:
    if (state->lock) {
      xSemaphoreTakeRecursive(state->lock, portMAX_DELAY);
    }
    break;

  case EOS_DISPLAY_IOCTL_UNLOCK:
    if (state->lock) {
      xSemaphoreGiveRecursive(state->lock);
    }
    break;

  default:
    errno = ENOTTY;
    ret = -1;
    break;
  }

  return ret;
}

void driver_display_qemu_rgb_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state)
    return;

  qemu_rgb_state_t *state = dev->state;
  if (state->lock) {
    vSemaphoreDelete(state->lock);
    state->lock = NULL;
  }
  if (state->panel) {
    esp_lcd_panel_disp_on_off(state->panel, false);
    esp_lcd_panel_del(state->panel);
  }

  free(state);
  dev->state = NULL;
}

EOS_DRV_ATTR eos_drv_t driver_display_qemu_rgb = {
    EOS_DRV_INIT,
    .scope = "display",
    .name = "qemu_rgb",
    .devname = "fb",
    .init = driver_display_qemu_rgb_init,
    .write = driver_display_qemu_rgb_write,
    .ioctl = driver_display_qemu_rgb_ioctl,
    .shutdown = driver_display_qemu_rgb_shutdown};

#endif
