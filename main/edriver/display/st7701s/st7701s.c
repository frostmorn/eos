#include "includes.h"
#ifdef EOS_DRV_DISPLAY_ST7701S_ENABLED

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_st7701.h>
#include <esp_ldo_regulator.h>

#include "ecore/capsmgr.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

// ── State ─────────────────────────────────────────────────────

typedef struct {
  esp_lcd_dsi_bus_handle_t dsi_bus;
  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_handle_t panel;
  esp_ldo_channel_handle_t ldo_phy;
  int32_t width;
  int32_t height;
  uint8_t rotation;
  uint32_t clip_x;
  uint32_t clip_y;
  uint32_t clip_w;
  uint32_t clip_h;
  bool has_clip;
  int32_t bl_pin;
  int32_t rst_pin;
  bool bl_ledc_active;
} st7701s_state_t;

// JC4880P443C 4.3" 480x800 ST7701S initialization sequence
static const st7701_lcd_init_cmd_t s_st7701_init_cmds_480_800[] = {
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t []){0x08}, 1, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t []){0x63, 0x00}, 2, 0}, // 800 lines
    {0xC1, (uint8_t []){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t []){0x10, 0x08}, 2, 0},
    {0xCC, (uint8_t []){0x10}, 1, 0},

    {0xB0, (uint8_t []){0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09, 0x09, 0x28, 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71}, 16, 0},
    {0xB1, (uint8_t []){0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08, 0x08, 0x25, 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D}, 16, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},

    {0xB0, (uint8_t []){0x5D}, 1, 0},
    {0xB1, (uint8_t []){0x58}, 1, 0},
    {0xB2, (uint8_t []){0x87}, 1, 0},
    {0xB3, (uint8_t []){0x80}, 1, 0},
    {0xB5, (uint8_t []){0x4E}, 1, 0},
    {0xB7, (uint8_t []){0x85}, 1, 0},
    {0xB8, (uint8_t []){0x21}, 1, 0},
    {0xB9, (uint8_t []){0x10, 0x1F}, 2, 0},
    {0xBB, (uint8_t []){0x03}, 1, 0},
    {0xBC, (uint8_t []){0x00}, 1, 0},

    {0xC1, (uint8_t []){0x78}, 1, 0},
    {0xC2, (uint8_t []){0x78}, 1, 0},
    {0xD0, (uint8_t []){0x88}, 1, 0},

    {0xE0, (uint8_t []){0x00, 0x3A, 0x02}, 3, 0},
    {0xE1, (uint8_t []){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40}, 11, 0},
    {0xE2, (uint8_t []){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00}, 13, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}, 16, 0},

    {0xEB, (uint8_t []){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0},
    {0xEC, (uint8_t []){0x08, 0x01}, 2, 0},

    {0xED, (uint8_t []){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}, 16, 0},
    {0xEF, (uint8_t []){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},

    {0x11, (uint8_t []){0x00}, 1, 120},
    {0x29, (uint8_t []){0x00}, 1, 20},
};

// ── Backlight helper ──────────────────────────────────────────

static void st7701s_backlight_init(int32_t bl_pin, bool *out_ledc_active) {
  if (bl_pin < 0) return;

  const ledc_timer_config_t bl_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = LEDC_TIMER_1,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  if (ledc_timer_config(&bl_timer) == ESP_OK) {
    const ledc_channel_config_t bl_chan = {
        .gpio_num = bl_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
    };
    if (ledc_channel_config(&bl_chan) == ESP_OK) {
      if (out_ledc_active) *out_ledc_active = true;
      return;
    }
  }

  // Fallback to plain GPIO
  gpio_set_direction(bl_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(bl_pin, 0);
  if (out_ledc_active) *out_ledc_active = false;
}

static void st7701s_backlight_on(int32_t bl_pin, bool ledc_active) {
  if (bl_pin < 0) return;
  if (ledc_active) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023); // 100%
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  } else {
    gpio_set_direction(bl_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(bl_pin, 1);
  }
}

static void st7701s_backlight_off(int32_t bl_pin, bool ledc_active) {
  if (bl_pin < 0) return;
  if (ledc_active) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  } else {
    gpio_set_level(bl_pin, 0);
  }
}

// ── Driver functions ──────────────────────────────────────────

bool driver_display_st7701s_init(eos_dev_t *dev) {
  st7701s_state_t *state = calloc(1, sizeof(st7701s_state_t));
  if (!state)
    return false;
  dev->state = state;

  int32_t native_w = eos_cfg_get_i(dev->cfg, "width", 480);
  int32_t native_h = eos_cfg_get_i(dev->cfg, "height", 800);
  uint8_t rotation = eos_cfg_get_i(dev->cfg, "rotation", 0);
  int32_t lane_num = eos_cfg_get_i(dev->cfg, "lane_num", 2);
  int32_t bitrate_mbps = eos_cfg_get_i(dev->cfg, "bitrate_mbps", 750);
  int32_t dpi_clk_mhz = eos_cfg_get_i(dev->cfg, "dpi_clk_mhz", 28);
  int32_t ldo_chan = eos_cfg_get_i(dev->cfg, "ldo_chan", 3);
  int32_t ldo_mv = eos_cfg_get_i(dev->cfg, "ldo_voltage_mv", 2500);

  int32_t rst_pin = eos_pin_get_no(dev->pins, "rst");
  int32_t bl_pin = eos_pin_get_no(dev->pins, "backlight");
  if (bl_pin < 0) {
    bl_pin = eos_pin_get_no(dev->pins, "backlight_pwm");
  }

  state->bl_pin = bl_pin;
  state->rst_pin = rst_pin;
  state->width = native_w;
  state->height = native_h;
  state->rotation = rotation;

  // Claim GPIOs
  if (rst_pin >= 0) {
    if (!eos_cap_claim(EOS_CAPS_GPIO, rst_pin, dev)) {
      free(state);
      dev->state = NULL;
      return false;
    }
  }

  if (bl_pin >= 0) {
    if (!eos_cap_claim(EOS_CAPS_GPIO, bl_pin, dev)) {
      if (rst_pin >= 0)
        eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
      free(state);
      dev->state = NULL;
      return false;
    }
    st7701s_backlight_init(bl_pin, &state->bl_ledc_active);
  }

  // Supply power to MIPI DSI PHY via internal LDO if configured
  if (ldo_chan >= 0) {
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = ldo_chan,
        .voltage_mv = ldo_mv,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &state->ldo_phy);
    if (err != ESP_OK) {
      EOS_LOGE("st7701s: acquire LDO chan %d failed: %s", (int)ldo_chan, esp_err_to_name(err));
      state->ldo_phy = NULL;
    } else {
      EOS_LOGI("st7701s: MIPI DSI PHY powered via LDO %d (%dmV)", (int)ldo_chan, (int)ldo_mv);
    }
  }

  // Create MIPI DSI bus
  // Note: phy_clk_src = 0 allows esp_lcd to automatically choose XTAL for rev >= 3.0
  // or PLL_F20M for rev < 3.0 on ESP32-P4 without triggering abort().
  esp_lcd_dsi_bus_config_t bus_config = {
      .bus_id = 0,
      .num_data_lanes = lane_num,
      .phy_clk_src = 0,
      .lane_bit_rate_mbps = bitrate_mbps,
  };
  esp_err_t err = esp_lcd_new_dsi_bus(&bus_config, &state->dsi_bus);
  if (err != ESP_OK) {
    EOS_LOGE("st7701s: new DSI bus failed: %s", esp_err_to_name(err));
    if (state->ldo_phy)
      esp_ldo_release_channel(state->ldo_phy);
    if (bl_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, bl_pin, dev);
    if (rst_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    free(state);
    dev->state = NULL;
    return false;
  }

  // Create DBI IO for commands/parameters
  esp_lcd_dbi_io_config_t dbi_config = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  err = esp_lcd_new_panel_io_dbi(state->dsi_bus, &dbi_config, &state->io);
  if (err != ESP_OK) {
    EOS_LOGE("st7701s: new panel IO DBI failed: %s", esp_err_to_name(err));
    esp_lcd_del_dsi_bus(state->dsi_bus);
    if (state->ldo_phy)
      esp_ldo_release_channel(state->ldo_phy);
    if (bl_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, bl_pin, dev);
    if (rst_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    free(state);
    dev->state = NULL;
    return false;
  }

  // Configure DPI video interface with exact JC4880P443C timings
  esp_lcd_dpi_panel_config_t dpi_config = {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
      .dpi_clock_freq_mhz = dpi_clk_mhz,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .out_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 1,
      .video_timing = {
          .h_size = native_w,
          .v_size = native_h,
          .hsync_back_porch = 42,
          .hsync_pulse_width = 12,
          .hsync_front_porch = 42,
          .vsync_back_porch = 8,
          .vsync_pulse_width = 2,
          .vsync_front_porch = 166,
      },
  };

  st7701_vendor_config_t vendor_config = {
      .init_cmds = (native_w == 480 && native_h == 800)
                       ? s_st7701_init_cmds_480_800
                       : NULL,
      .init_cmds_size =
          (native_w == 480 && native_h == 800)
              ? (sizeof(s_st7701_init_cmds_480_800) / sizeof(st7701_lcd_init_cmd_t))
              : 0,
      .flags = {
          .use_mipi_interface = 1,
      },
      .mipi_config = {
          .dsi_bus = state->dsi_bus,
          .dpi_config = &dpi_config,
      },
  };

  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = rst_pin,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
      .vendor_config = &vendor_config,
  };

  err = esp_lcd_new_panel_st7701(state->io, &panel_cfg, &state->panel);
  if (err != ESP_OK) {
    EOS_LOGE("st7701s: new panel ST7701 failed: %s", esp_err_to_name(err));
    esp_lcd_panel_io_del(state->io);
    esp_lcd_del_dsi_bus(state->dsi_bus);
    if (state->ldo_phy)
      esp_ldo_release_channel(state->ldo_phy);
    if (bl_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, bl_pin, dev);
    if (rst_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    free(state);
    dev->state = NULL;
    return false;
  }

  err = esp_lcd_panel_reset(state->panel);
  if (err != ESP_OK) {
    EOS_LOGE("st7701s: panel reset failed: %s", esp_err_to_name(err));
  }
  // Allow hardware reset sequence to fully settle
  vTaskDelay(pdMS_TO_TICKS(120));

  err = esp_lcd_panel_init(state->panel);
  if (err != ESP_OK) {
    EOS_LOGE("st7701s: panel init failed: %s", esp_err_to_name(err));
  } else {
    EOS_LOGI("st7701s: panel initialized (%dx%d, %d MHz DPI)", (int)native_w, (int)native_h, (int)dpi_clk_mhz);
  }

  err = esp_lcd_panel_disp_on_off(state->panel, true);
  if (err != ESP_OK) {
    EOS_LOGE("st7701s: display ON failed: %s", esp_err_to_name(err));
  }

  if (bl_pin >= 0) {
    st7701s_backlight_on(bl_pin, state->bl_ledc_active);
    EOS_LOGI("st7701s: backlight ON on GPIO %d (LEDC=%d)", (int)bl_pin, state->bl_ledc_active);
  }

  return true;
}

ssize_t driver_display_st7701s_write(eos_dev_t *dev, int fd, const void *buf, size_t len) {
  (void)fd;
  if (!dev || !dev->state || !buf || len == 0) {
    errno = EINVAL;
    return -1;
  }

  st7701s_state_t *state = dev->state;
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
  if (len > expected_len)
    len = expected_len;

  size_t row_bytes = (size_t)w * 2;
  if (row_bytes == 0 || len < row_bytes) {
    errno = EINVAL;
    return -1;
  }

  uint32_t total_rows = (uint32_t)(len / row_bytes);
  if (total_rows == 0)
    return 0;
  if (total_rows > h)
    total_rows = h;

  if (esp_lcd_panel_draw_bitmap(state->panel, x, y, x + w, y + total_rows, buf) != ESP_OK) {
    return -1;
  }
  return (ssize_t)((size_t)total_rows * row_bytes);
}

int driver_display_st7701s_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  st7701s_state_t *state = dev->state;
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

void driver_display_st7701s_shutdown(eos_dev_t *dev) {
  st7701s_state_t *state = dev->state;
  if (!state)
    return;

  if (state->bl_pin >= 0) {
    st7701s_backlight_off(state->bl_pin, state->bl_ledc_active);
  }

  if (state->panel) {
    esp_lcd_panel_disp_on_off(state->panel, false);
    esp_lcd_panel_del(state->panel);
  }
  if (state->io) {
    esp_lcd_panel_io_del(state->io);
  }
  if (state->dsi_bus) {
    esp_lcd_del_dsi_bus(state->dsi_bus);
  }
  if (state->ldo_phy) {
    esp_ldo_release_channel(state->ldo_phy);
  }

  if (state->bl_pin >= 0) {
    eos_cap_release(EOS_CAPS_GPIO, state->bl_pin, dev);
  }
  if (state->rst_pin >= 0) {
    eos_cap_release(EOS_CAPS_GPIO, state->rst_pin, dev);
  }

  free(state);
  dev->state = NULL;
}

EOS_DRV_ATTR eos_drv_t driver_display_st7701s = {
    EOS_DRV_INIT,
    .scope = "display",
    .name = "st7701s",
    .devname = "fb",
    .init = driver_display_st7701s_init,
    .write = driver_display_st7701s_write,
    .ioctl = driver_display_st7701s_ioctl,
    .shutdown = driver_display_st7701s_shutdown};

#endif
