#include "includes.h"
#ifdef EOS_DRV_DISPLAY_ST7789_ENABLED

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ecore/capsmgr.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "emisc/psram_alloc.h"

// ── State ─────────────────────────────────────────────────────

typedef struct {
  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_handle_t panel;
  int32_t width;  // width after rotation
  int32_t height; // height after rotation
  uint8_t rotation;
  eos_clip_rect_t clip;
  uint8_t *dma_buf[2];
  uint8_t dma_idx;
  SemaphoreHandle_t lock;
} st7789_state_t;

// Placement options
static void st7789_apply_placement(st7789_state_t *state, int32_t native_w,
                                   int32_t native_h, int32_t native_gap_x,
                                   int32_t native_gap_y, uint8_t rotation) {
  switch (rotation & 3) {
  case 0:
  default:
    esp_lcd_panel_swap_xy(state->panel, false);
    esp_lcd_panel_mirror(state->panel, false, false);
    esp_lcd_panel_set_gap(state->panel, native_gap_x, native_gap_y);
    state->width = native_w;
    state->height = native_h;
    break;

  case 1:
    esp_lcd_panel_swap_xy(state->panel, true);
    esp_lcd_panel_mirror(state->panel, true, false);
    esp_lcd_panel_set_gap(state->panel, native_gap_y, native_gap_x);
    state->width = native_h;
    state->height = native_w;
    break;

  case 2:
    esp_lcd_panel_swap_xy(state->panel, false);
    esp_lcd_panel_mirror(state->panel, true, true);
    esp_lcd_panel_set_gap(state->panel, native_gap_x, native_gap_y);
    state->width = native_w;
    state->height = native_h;
    break;

  case 3:
    esp_lcd_panel_swap_xy(state->panel, true);
    esp_lcd_panel_mirror(state->panel, false, true);
    esp_lcd_panel_set_gap(state->panel, native_gap_y, native_gap_x);
    state->width = native_h;
    state->height = native_w;
    break;
  }
}
// ── Driver functions ──────────────────────────────────────────

static bool st7789_claim_pins(eos_dev_t *dev, int32_t cs_pin, int32_t dc_pin, int32_t en_pin, int32_t rst_pin) {
  if (!eos_cap_claim(EOS_CAPS_GPIO, cs_pin, dev)) return false;
  if (!eos_cap_claim(EOS_CAPS_GPIO, dc_pin, dev)) {
    eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
    return false;
  }
  if (en_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, en_pin, dev)) {
    eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
    return false;
  }
  if (rst_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, rst_pin, dev)) {
    if (en_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, en_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
    return false;
  }
  return true;
}

static void st7789_release_pins(eos_dev_t *dev, int32_t cs_pin, int32_t dc_pin, int32_t en_pin, int32_t rst_pin) {
  if (en_pin >= 0) {
    gpio_set_level(en_pin, 0);
    eos_cap_release(EOS_CAPS_GPIO, en_pin, dev);
  }
  if (rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
  eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
  eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
}

bool driver_display_st7789_init(eos_dev_t *dev) {
  st7789_state_t *state = eos_psram_malloc_prefer(sizeof(st7789_state_t));
  if (!state)
    return false;
  memset(state, 0, sizeof(st7789_state_t));
  dev->state = state;

  // Native dimensions from config (before rotation)
  int32_t native_w = eos_cfg_get_i(dev->cfg, "width", EOS_DISPLAY_DEFAULT_HEIGHT);
  int32_t native_h = eos_cfg_get_i(dev->cfg, "height", EOS_DISPLAY_DEFAULT_WIDTH);
  int32_t native_gap_x =
      eos_cfg_get_i(dev->cfg, "gap_x", (ST7789_GRAM_WIDTH - native_w) / 2);
  int32_t native_gap_y =
      eos_cfg_get_i(dev->cfg, "gap_y", (ST7789_GRAM_HEIGHT - native_h) / 2);

  uint8_t rotation = eos_cfg_get_i(dev->cfg, "rotation", 0);

  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  int32_t dc_pin = eos_pin_get_no(dev->pins, "dc");
  int32_t en_pin = eos_pin_get_no(dev->pins, "en");
  int32_t rst_pin = eos_pin_get_no(dev->pins, "rst");

  if (!st7789_claim_pins(dev, cs_pin, dc_pin, en_pin, rst_pin)) {
    free(state);
    dev->state = NULL;
    return false;
  }

  // Enable display power before anything else
  if (en_pin >= 0) {
    gpio_set_direction(en_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(en_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  spi_host_device_t host = SPI2_HOST;
  if (dev->parent) {
    if (eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_SPI_IOCTL_GET_HOST, &host) != 0) {
      host = (spi_host_device_t)eos_cfg_get_i(dev->parent->cfg, "host", SPI2_HOST);
    }
  }
  int32_t freq = eos_cfg_get_i(dev->cfg, "clock_speed_hz", SPI_MASTER_FREQ_40M);

  // Create LCD SPI IO — esp_lcd handles DC toggling internally
  esp_lcd_panel_io_spi_config_t io_cfg = {
      .dc_gpio_num = dc_pin,
      .cs_gpio_num = cs_pin,
      .pclk_hz = freq,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .spi_mode = 0,
      .trans_queue_depth = 10,
  };

  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)host, &io_cfg,
                               &state->io) != ESP_OK) {
    st7789_release_pins(dev, cs_pin, dc_pin, en_pin, rst_pin);
    free(state);
    dev->state = NULL;
    return false;
  }

  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = rst_pin,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
      .bits_per_pixel = 16,
  };

  if (esp_lcd_new_panel_st7789(state->io, &panel_cfg, &state->panel) !=
      ESP_OK) {
    st7789_release_pins(dev, cs_pin, dc_pin, en_pin, rst_pin);
    esp_lcd_panel_io_del(state->io);
    free(state);
    dev->state = NULL;
    return false;
  }

  // Pre-allocate two persistent ping-pong DMA strip buffers so asynchronous
  // queued SPI DMA transfers are never overwritten or freed while in flight.
  state->dma_buf[0] =
      heap_caps_malloc(ST7789_DMA_STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  state->dma_buf[1] =
      heap_caps_malloc(ST7789_DMA_STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  state->dma_idx = 0;
  state->lock = xSemaphoreCreateRecursiveMutex();

  esp_lcd_panel_reset(state->panel);
  esp_lcd_panel_init(state->panel);

  esp_lcd_panel_invert_color(state->panel, true);

  st7789_apply_placement(state, native_w, native_h, native_gap_x, native_gap_y,
                         rotation);

  esp_lcd_panel_disp_on_off(state->panel, true);
#ifdef EOS_DRV_DISPLAY_ST7789_TEST
  st7789_test(dev);
  // st7789_test2(dev);
#endif
  return true;
}

ssize_t driver_display_st7789_write(eos_dev_t *dev, int fd, const void *buf, size_t len) {
  (void)fd;
  size_t bytes_per_pixel = eos_display_format_bytes_per_pixel(EOS_DISPLAY_FMT_RGB565);
  if (!dev || !dev->state || !buf || len == 0 || (len % bytes_per_pixel) != 0) {
    errno = EINVAL;
    return -1;
  }

  st7789_state_t *st = dev->state;
  eos_rect_t r;
  eos_clip_rect_consume(&st->clip, (uint32_t)st->width, (uint32_t)st->height, &r);

  uint32_t total_rows = eos_rect_calc_rows(len, r.w, bytes_per_pixel, r.h);
  if (total_rows == 0) {
    errno = EINVAL;
    return -1;
  }

  if (st->lock) {
    xSemaphoreTakeRecursive(st->lock, portMAX_DELAY);
  }
  size_t row_bytes = (size_t)r.w * bytes_per_pixel;

  // Transfer in horizontal strips using persistent ping-pong DMA buffers.
  // Because esp_lcd_panel_draw_bitmap sends CASET/RASET synchronously via
  // polling transmit before queuing RAMWR color data, alternating between
  // dma_buf[0] and dma_buf[1] guarantees the previous strip's DMA transfer
  // has completed before its buffer is reused on the next strip.
  if (st->dma_buf[0] && st->dma_buf[1] &&
      row_bytes <= ST7789_DMA_STRIP_BYTES) {
    uint32_t strip_h = (uint32_t)(ST7789_DMA_STRIP_BYTES / row_bytes);
    if (strip_h == 0)
      strip_h = 1;
    if (strip_h > total_rows)
      strip_h = total_rows;

    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t sy = 0; sy < total_rows; sy += strip_h) {
      uint32_t sh =
          (sy + strip_h <= total_rows) ? strip_h : (total_rows - sy);
      size_t chunk_len = (size_t)sh * row_bytes;
      uint8_t *dst = st->dma_buf[st->dma_idx & 1U];
      st->dma_idx ^= 1U;

      memcpy(dst, src + (size_t)sy * row_bytes, chunk_len);
      if (esp_lcd_panel_draw_bitmap(st->panel, r.x, r.y + sy, r.x + r.w,
                                    r.y + sy + sh, dst) != ESP_OK) {
        if (st->lock) xSemaphoreGiveRecursive(st->lock);
        return -1;
      }
    }
    if (st->lock) xSemaphoreGiveRecursive(st->lock);
    return (ssize_t)((size_t)total_rows * row_bytes);
  }

  // Fallback if persistent ping-pong buffers are unavailable
  if (esp_lcd_panel_draw_bitmap(st->panel, r.x, r.y, r.x + r.w, r.y + total_rows,
                                buf) != ESP_OK) {
    if (st->lock) xSemaphoreGiveRecursive(st->lock);
    return -1;
  }
  if (st->lock) xSemaphoreGiveRecursive(st->lock);
  return (ssize_t)((size_t)total_rows * row_bytes);
}

int driver_display_st7789_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  st7789_state_t *st = dev->state;
  if (!st) {
    errno = ENODEV;
    return -1;
  }

  switch (cmd) {
  case EOS_DISPLAY_IOCTL_GET_WIDTH: {
    uint32_t *w = va_arg(args, uint32_t *);
    if (!w) { errno = EINVAL; return -1; }
    *w = st->width;
    return 0;
  }
  case EOS_DISPLAY_IOCTL_GET_HEIGHT: {
    uint32_t *h = va_arg(args, uint32_t *);
    if (!h) { errno = EINVAL; return -1; }
    *h = st->height;
    return 0;
  }
  case EOS_DISPLAY_IOCTL_SET_WINDOW:
    return eos_clip_rect_set(&st->clip, va_arg(args, const eos_display_rect_t *),
                            (uint32_t)st->width, (uint32_t)st->height);
  case EOS_DISPLAY_IOCTL_GET_WINDOW:
    return eos_clip_rect_get(&st->clip, (uint32_t)st->width, (uint32_t)st->height,
                            va_arg(args, eos_display_rect_t *));
  case EOS_DISPLAY_IOCTL_LOCK:
    if (st->lock) xSemaphoreTakeRecursive(st->lock, portMAX_DELAY);
    return 0;

  case EOS_DISPLAY_IOCTL_UNLOCK:
    if (st->lock) xSemaphoreGiveRecursive(st->lock);
    return 0;

  case EOS_DISPLAY_IOCTL_GET_FORMATS: {
    uint32_t *fmts = va_arg(args, uint32_t *);
    if (!fmts) { errno = EINVAL; return -1; }
    *fmts = EOS_DISPLAY_FMT_MASK_RGB565;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_GET_FORMAT: {
    uint32_t *fmt = va_arg(args, uint32_t *);
    if (!fmt) { errno = EINVAL; return -1; }
    *fmt = EOS_DISPLAY_FMT_RGB565;
    return 0;
  }

  case EOS_DISPLAY_IOCTL_SET_FORMAT: {
    uint32_t fmt = va_arg(args, uint32_t);
    if (fmt == EOS_DISPLAY_FMT_RGB565) return 0;
    errno = ENOTSUP;
    return -1;
  }

  default:
    errno = ENOTTY;
    return -1;
  }
}

void driver_display_st7789_shutdown(eos_dev_t *dev) {
  st7789_state_t *state = dev->state;
  if (!state)
    return;

  if (state->lock) {
    vSemaphoreDelete(state->lock);
    state->lock = NULL;
  }

  int32_t en_pin = eos_pin_get_no(dev->pins, "en");
  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  int32_t dc_pin = eos_pin_get_no(dev->pins, "dc");
  int32_t rst_pin = eos_pin_get_no(dev->pins, "rst");

  if (state->panel) {
    esp_lcd_panel_disp_on_off(state->panel, false);
    esp_lcd_panel_del(state->panel);
  }
  if (state->io) {
    esp_lcd_panel_io_del(state->io);
  }

  if (state->dma_buf[0]) {
    free(state->dma_buf[0]);
    state->dma_buf[0] = NULL;
  }
  if (state->dma_buf[1]) {
    free(state->dma_buf[1]);
    state->dma_buf[1] = NULL;
  }

  if (en_pin >= 0) {
    gpio_set_level(en_pin, 0);
    eos_cap_release(EOS_CAPS_GPIO, en_pin, dev);
  }

  eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
  eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
  if (rst_pin >= 0)
    eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);

  free(state);
  dev->state = NULL;
}

EOS_DRV_ATTR eos_drv_t driver_display_st7789 = {
    EOS_DRV_INIT,
    .scope = "display",
    .name = "st7789",
    .devname = "fb",
    .init = driver_display_st7789_init,
    .write = driver_display_st7789_write,
    .ioctl = driver_display_st7789_ioctl,
    .shutdown = driver_display_st7789_shutdown};

#endif
