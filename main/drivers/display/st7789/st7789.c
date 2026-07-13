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

// ── Register definitions ──────────────────────────────────────

#define ST7789_SLPOUT 0x11
#define ST7789_NORON 0x13
#define ST7789_INVOFF 0x20
#define ST7789_INVON 0x21
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_COLMOD 0x3A
#define ST7789_MADCTL 0x36

#define ST7789_MADCTL_MY 0x80
#define ST7789_MADCTL_MX 0x40
#define ST7789_MADCTL_MV 0x20
#define ST7789_MADCTL_ML 0x10
#define ST7789_MADCTL_RGB 0x00

#define ST7789_SLPOUT_DELAY_MS 120
#define ST7789_COLOR_MODE_16BIT 0x55

// ── Rotation table ────────────────────────────────────────────
// Each entry: {madctl_byte, swap_dims}
// Native display is portrait (240 wide x 280 tall)
// Rotations 1 and 3 are landscape — swap w/h

typedef struct {
  uint8_t madctl;
  bool swap_dims;
} st7789_rotation_t;

static const st7789_rotation_t st7789_rotations[] = {
    [0] = {ST7789_MADCTL_RGB, false}, // portrait
    [1] = {ST7789_MADCTL_MX | ST7789_MADCTL_MV | ST7789_MADCTL_RGB,
           true}, // landscape CW
    [2] = {ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_RGB,
           false}, // portrait inverted
    [3] = {ST7789_MADCTL_MY | ST7789_MADCTL_MV | ST7789_MADCTL_RGB,
           true}, // landscape CCW
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

// ── Init sequence ─────────────────────────────────────────────

static void st7789_run_init(esp_lcd_panel_io_handle_t io) {
  esp_lcd_panel_io_tx_param(io, ST7789_SLPOUT, NULL, 0);
  vTaskDelay(pdMS_TO_TICKS(ST7789_SLPOUT_DELAY_MS));

  // Color mode 16-bit — MADCTL omitted, set via st7789_apply_rotation
  esp_lcd_panel_io_tx_param(io, ST7789_COLMOD,
                            (uint8_t[]){ST7789_COLOR_MODE_16BIT}, 1);

  esp_lcd_panel_io_tx_param(io, 0xB2, (uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33},
                            5);

  esp_lcd_panel_io_tx_param(io, 0xB7, (uint8_t[]){0x35}, 1);
  esp_lcd_panel_io_tx_param(io, 0xBB, (uint8_t[]){0x19}, 1);
  esp_lcd_panel_io_tx_param(io, 0xC0, (uint8_t[]){0x2C}, 1);
  esp_lcd_panel_io_tx_param(io, 0xC2, (uint8_t[]){0x01}, 1);
  esp_lcd_panel_io_tx_param(io, 0xC3, (uint8_t[]){0x12}, 1);
  esp_lcd_panel_io_tx_param(io, 0xC4, (uint8_t[]){0x20}, 1);
  esp_lcd_panel_io_tx_param(io, 0xC6, (uint8_t[]){0x0F}, 1);
  esp_lcd_panel_io_tx_param(io, 0xD0, (uint8_t[]){0xA4, 0xA1}, 2);

  esp_lcd_panel_io_tx_param(io, 0xE0,
                            (uint8_t[]){0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B,
                                        0x3C, 0x44, 0x4B, 0x1B, 0x18, 0x17,
                                        0x1D, 0x21},
                            14);

  esp_lcd_panel_io_tx_param(io, 0xE1,
                            (uint8_t[]){0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27,
                                        0x3B, 0x44, 0x4D, 0x0B, 0x17, 0x17,
                                        0x1D, 0x21},
                            14);

  esp_lcd_panel_io_tx_param(io, ST7789_NORON, NULL, 0);
  vTaskDelay(pdMS_TO_TICKS(10));

  esp_lcd_panel_io_tx_param(io, ST7789_DISPON, NULL, 0);
}

// ── Rotation ──────────────────────────────────────────────────

static void st7789_apply_rotation(st7789_state_t *state, int32_t native_w,
                                  int32_t native_h, uint8_t rotation) {
  const st7789_rotation_t *r = &st7789_rotations[rotation % 4];
  state->rotation = rotation % 4;

  // Swap effective dimensions for landscape rotations
  if (r->swap_dims) {
    state->width = native_h;
    state->height = native_w;
  } else {
    state->width = native_w;
    state->height = native_h;
  }

  esp_lcd_panel_io_tx_param(state->io, ST7789_MADCTL, (uint8_t[]){r->madctl},
                            1);
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
  st7789_run_init(state->io);

  // Apply rotation last — sets MADCTL and effective dimensions
  st7789_apply_rotation(state, native_w, native_h, rotation);

  st7789_test(dev);
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