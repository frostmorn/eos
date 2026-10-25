#include "includes.h"
#ifdef EOS_DRV_DISPLAY_ST7789_ENABLED

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include "ecore/capsmgr.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

// Actual Display dimensions maxima defined by st7789 datasheets
// We need to know those values to deduce an x/y shifts to make it actually work
#define ST7789_GRAM_WIDTH 240
#define ST7789_GRAM_HEIGHT 320

// ── State ─────────────────────────────────────────────────────

typedef struct {
  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_handle_t panel;
  int32_t width;  // width after rotation
  int32_t height; // height after rotation
  uint8_t rotation;
  uint32_t clip_x;
  uint32_t clip_y;
  uint32_t clip_w;
  uint32_t clip_h;
  bool has_clip;
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

bool driver_display_st7789_init(eos_dev_t *dev) {
  st7789_state_t *state = malloc(sizeof(st7789_state_t));
  if (!state)
    return false;
  memset(state, 0, sizeof(st7789_state_t));
  dev->state = state;

  // Native dimensions from config (before rotation)
  int32_t native_w = eos_cfg_get_i(dev->cfg, "width", 240);
  int32_t native_h = eos_cfg_get_i(dev->cfg, "height", 280);
  int32_t native_gap_x =
      eos_cfg_get_i(dev->cfg, "gap_x", (ST7789_GRAM_WIDTH - native_w) / 2);
  int32_t native_gap_y =
      eos_cfg_get_i(dev->cfg, "gap_y", (ST7789_GRAM_HEIGHT - native_h) / 2);

  uint8_t rotation = eos_cfg_get_i(dev->cfg, "rotation", 0);

  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  int32_t dc_pin = eos_pin_get_no(dev->pins, "dc");
  int32_t en_pin = eos_pin_get_no(dev->pins, "en");
  int32_t rst_pin = eos_pin_get_no(dev->pins, "rst");

  // Claim pins with proper cleanup on failure
  if (!eos_cap_claim(EOS_CAPS_GPIO, cs_pin, dev)) {
    free(state);
    dev->state = NULL;
    return false;
  }
  if (!eos_cap_claim(EOS_CAPS_GPIO, dc_pin, dev)) {
    eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
    free(state);
    dev->state = NULL;
    return false;
  }
  if (en_pin >= 0) {
    if (!eos_cap_claim(EOS_CAPS_GPIO, en_pin, dev)) {
      eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
      eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
      free(state);
      dev->state = NULL;
      return false;
    }
  }
  if (rst_pin >= 0) {
    if (!eos_cap_claim(EOS_CAPS_GPIO, rst_pin, dev)) {
      if (en_pin >= 0)
        eos_cap_release(EOS_CAPS_GPIO, en_pin, dev);
      eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
      eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
      free(state);
      dev->state = NULL;
      return false;
    }
  }

  // Enable display power before anything else
  if (en_pin >= 0) {
    gpio_set_direction(en_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(en_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  spi_host_device_t host =
      (spi_host_device_t)eos_cfg_get_i(dev->parent->cfg, "host", SPI2_HOST);
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
    if (en_pin >= 0) {
      gpio_set_level(en_pin, 0);
      eos_cap_release(EOS_CAPS_GPIO, en_pin, dev);
    }
    if (rst_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
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
    if (en_pin >= 0) {
      gpio_set_level(en_pin, 0);
      eos_cap_release(EOS_CAPS_GPIO, en_pin, dev);
    }
    if (rst_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, dc_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
    esp_lcd_panel_io_del(state->io);
    free(state);
    dev->state = NULL;
    return false;
  }

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
  if (!dev || !dev->state || !buf || len == 0) {
    errno = EINVAL;
    return -1;
  }

  st7789_state_t *state = dev->state;
  uint32_t x = state->has_clip ? state->clip_x : 0;
  uint32_t y = state->has_clip ? state->clip_y : 0;
  uint32_t w = state->has_clip ? state->clip_w : (uint32_t)state->width;
  uint32_t h = state->has_clip ? state->clip_h : (uint32_t)state->height;
  state->has_clip = false;

  if (w == 0 || h == 0 || x + w > (uint32_t)state->width ||
      y + h > (uint32_t)state->height) {
    errno = EINVAL;
    return -1;
  }

  size_t expected_len = (size_t)w * h * 2;
  if (len > expected_len) len = expected_len;

  // Try allocating DMA-capable memory for the bitmap transfer
  void *dma_buf = heap_caps_malloc(len, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (dma_buf) {
    memcpy(dma_buf, buf, len);
    ssize_t ret = esp_lcd_panel_draw_bitmap(state->panel, x, y, x + w, y + h, dma_buf) == ESP_OK ? (ssize_t)len : -1;
    free(dma_buf);
    return ret;
  }

  // If DMA allocation failed (e.g. too large), send in row strips
  size_t row_bytes = (size_t)w * 2;
  size_t strip_h = 16384 / row_bytes;
  if (strip_h == 0) strip_h = 1;
  if (strip_h > h) strip_h = h;

  void *strip_buf = heap_caps_malloc(strip_h * row_bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!strip_buf) {
    return esp_lcd_panel_draw_bitmap(state->panel, x, y, x + w, y + h, buf) == ESP_OK ? (ssize_t)len : -1;
  }

  const uint8_t *src = buf;
  ssize_t ret = (ssize_t)len;
  for (uint32_t sy = 0; sy < h; sy += strip_h) {
    uint32_t sh = (sy + strip_h <= h) ? strip_h : (h - sy);
    size_t chunk_len = sh * row_bytes;
    memcpy(strip_buf, src + sy * row_bytes, chunk_len);
    if (esp_lcd_panel_draw_bitmap(state->panel, x, y + sy, x + w, y + sy + sh, strip_buf) != ESP_OK) {
      ret = -1;
      break;
    }
  }

  free(strip_buf);
  return ret;
}

int driver_display_st7789_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  st7789_state_t *state = dev->state;
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
    *width = state->width;
    break;
  }
  case EOS_DISPLAY_IOCTL_GET_HEIGHT: {
    uint32_t *height = va_arg(args, uint32_t *);
    if (!height) {
      errno = EINVAL;
      return -1;
    }
    *height = state->height;
    break;
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
    break;
  }
  default:
    errno = ENOTTY;
    ret = -1;
    break;
  }

  return ret;
}

void driver_display_st7789_shutdown(eos_dev_t *dev) {
  st7789_state_t *state = dev->state;
  if (!state)
    return;

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
