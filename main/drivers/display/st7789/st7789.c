#include "includes.h"
#ifdef EOS_DRIVER_DISPLAY_ST7789_ENABLED

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "drivers/bus/bus.h"
#include "sys/capsmgr.h"
#include "sys/driver.h"

typedef struct {
  bool swap_xy;
  bool mirror_x;
  bool mirror_y;
  bool swap_dims;
  int16_t x_gap;
  int16_t y_gap;
} st7789_rotation_t;

// Rotation 1 (90° CW)

static const st7789_rotation_t st7789_rotations[] = {
    {
        .swap_xy = true,
        .mirror_x = true,
        .mirror_y = false,
        .swap_dims = true,
        .x_gap = 0,
        .y_gap = 20,
    },

    // Rotation 2 (180°)
    {
        .swap_xy = false,
        .mirror_x = true,
        .mirror_y = true,
        .swap_dims = false,
        .x_gap = 20,
        .y_gap = 0,
    },

    // Rotation 3 (270° CW)
    {
        .swap_xy = true,
        .mirror_x = false,
        .mirror_y = true,
        .swap_dims = true,
        .x_gap = 20,
        .y_gap = 0,
    },
};
// ── State ─────────────────────────────────────────────────────

typedef struct {
  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_handle_t panel;
  int32_t width;  // effective width after rotation
  int32_t height; // effective height after rotation
  uint8_t rotation;
} st7789_state_t;

// ── Test ──────────────────────────────────────────────────────

static void st7789_test(eos_dev_t *dev) {
  st7789_state_t *state = dev->state;
  int32_t w = state->width;
  int32_t h = state->height;

  uint16_t *buf = malloc(w * h * sizeof(uint16_t));
  if (!buf)
    return;

  // Red
  for (int i = 0; i < w * h; i++)
    buf[i] = 0xF800;
  dev->driver->write(dev, buf, w * h * sizeof(uint16_t));
  vTaskDelay(pdMS_TO_TICKS(500));

  // Green
  for (int i = 0; i < w * h; i++)
    buf[i] = 0x07E0;
  dev->driver->write(dev, buf, w * h * sizeof(uint16_t));
  vTaskDelay(pdMS_TO_TICKS(500));

  // Blue
  for (int i = 0; i < w * h; i++)
    buf[i] = 0x001F;
  dev->driver->write(dev, buf, w * h * sizeof(uint16_t));
  vTaskDelay(pdMS_TO_TICKS(500));

  // Gradient
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      buf[y * w + x] = ((x * 31 / w) << 11) | ((y * 63 / h) << 5);
  dev->driver->write(dev, buf, w * h * sizeof(uint16_t));

  free(buf);
}

static void st7789_test2(eos_dev_t *dev) {
  st7789_state_t *state = dev->state;
  int32_t w = state->width;
  int32_t h = state->height;

  uint16_t *buf = malloc(w * h * sizeof(uint16_t));
  if (!buf)
    return;

  // RGB565 color bars
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint16_t c;

      if (x < w / 6)
        c = 0xF800; // red
      else if (x < w * 2 / 6)
        c = 0x07E0; // green
      else if (x < w * 3 / 6)
        c = 0x001F; // blue
      else if (x < w * 4 / 6)
        c = 0xFFE0; // yellow
      else if (x < w * 5 / 6)
        c = 0xF81F; // magenta
      else
        c = 0x07FF; // cyan

      // 1-pixel white border
      if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
        c = 0xFFFF;

      // White diagonals
      if (x == y || x == (w - 1 - y))
        c = 0xFFFF;

      buf[y * w + x] = c;
    }
  }

  dev->driver->write(dev, buf, w * h * sizeof(uint16_t));

  free(buf);
}

// ── Rotation ──────────────────────────────────────────────────

static void st7789_apply_rotation(st7789_state_t *state, int32_t native_w,
                                  int32_t native_h, uint8_t rotation) {
  const st7789_rotation_t *r = &st7789_rotations[rotation & 3];

  state->rotation = rotation & 3;

  if (r->swap_dims) {
    state->width = native_h;
    state->height = native_w;
  } else {
    state->width = native_w;
    state->height = native_h;
  }

  esp_lcd_panel_set_gap(state->panel, r->x_gap, r->y_gap);
  esp_lcd_panel_swap_xy(state->panel, r->swap_xy);
  esp_lcd_panel_mirror(state->panel, r->mirror_x, r->mirror_y);
}
// ── Driver functions ──────────────────────────────────────────

int driver_display_st7789_init(eos_dev_t *dev) {
  st7789_state_t *state = malloc(sizeof(st7789_state_t));
  if (!state)
    return -1;
  dev->state = state;

  // Native dimensions from config (before rotation)
  int32_t native_w = eos_cfg_get_i(dev->cfg, "width", 240);
  int32_t native_h = eos_cfg_get_i(dev->cfg, "height", 280);
  uint8_t rotation = eos_cfg_get_i(dev->cfg, "rotation", 0);

  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs_ena_pretrans");
  int32_t dc_pin = eos_pin_get_no(dev->pins, "dc_io_num");
  int32_t en_pin = eos_pin_get_no(dev->pins, "en_io_num");
  int32_t rst_pin = eos_pin_get_no(dev->pins, "rst_io_num");

  // Claim pins
  eos_cap_alloc(EOS_CAPS_GPIO, cs_pin, dev);
  eos_cap_alloc(EOS_CAPS_GPIO, dc_pin, dev);
  if (en_pin >= 0)
    eos_cap_alloc(EOS_CAPS_GPIO, en_pin, dev);
  if (rst_pin >= 0)
    eos_cap_alloc(EOS_CAPS_GPIO, rst_pin, dev);

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
    if (en_pin >= 0)
      gpio_set_level(en_pin, 0);
    free(state);
    return -1;
  }

  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = rst_pin,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
  };

  if (esp_lcd_new_panel_st7789(state->io, &panel_cfg, &state->panel) !=
      ESP_OK) {
    if (en_pin >= 0)
      gpio_set_level(en_pin, 0);
    esp_lcd_panel_io_del(state->io);
    free(state);
    return -1;
  }

  esp_lcd_panel_reset(state->panel);
  esp_lcd_panel_init(state->panel);

  st7789_apply_rotation(state, native_w, native_h, rotation);

  esp_lcd_panel_disp_on_off(state->panel, true);

  st7789_test(dev);
  // st7789_test2(dev);
  return 0;
}

int driver_display_st7789_write(eos_dev_t *dev, void *buf, size_t len) {
  st7789_state_t *state = dev->state;
  return esp_lcd_panel_draw_bitmap(state->panel, 0, 0, state->width,
                                   state->height, buf) == ESP_OK
             ? 0
             : -1;
}

int driver_display_st7789_read(eos_dev_t *dev, void *buf, size_t len) {
  return -1; // ST7789 is write-only
}

int driver_display_st7789_ioctl(eos_dev_t *dev, int cmd, ...) {
  va_list args;
  va_start(args, cmd);
  int ret = 0;

  switch (cmd) {
  case EOS_BUS_IOCTL_KID_ATTACH:
  case EOS_BUS_IOCTL_KID_DETACH:
    ret = false; // display is not a bus
    break;
  }

  va_end(args);
  return ret;
}

void driver_display_st7789_shutdown(eos_dev_t *dev) {
  st7789_state_t *state = dev->state;
  if (!state)
    return;

  int32_t en_pin = eos_pin_get_no(dev->pins, "en_io_num");
  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs_ena_pretrans");
  int32_t dc_pin = eos_pin_get_no(dev->pins, "dc_io_num");
  int32_t rst_pin = eos_pin_get_no(dev->pins, "rst_io_num");

  if (en_pin >= 0) {
    gpio_set_level(en_pin, 0);
    eos_cap_free(EOS_CAPS_GPIO, en_pin, dev);
  }

  esp_lcd_panel_del(state->panel);
  esp_lcd_panel_io_del(state->io);

  eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);
  eos_cap_free(EOS_CAPS_GPIO, dc_pin, dev);
  if (rst_pin >= 0)
    eos_cap_free(EOS_CAPS_GPIO, rst_pin, dev);

  free(state);
  dev->state = NULL;
}

EOS_DRIVER_REG(display, st7789, EOS_INIT_DRIVERS_PRIO);

#endif