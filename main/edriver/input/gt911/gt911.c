#include "includes.h"
#ifdef EOS_DRV_INPUT_GT911_ENABLED

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <errno.h>
#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch_gt911.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/input.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"

typedef struct {
  i2c_master_bus_handle_t i2c_bus;
  esp_lcd_panel_io_handle_t tp_io;
  esp_lcd_touch_handle_t tp;
  int sda_pin;
  int scl_pin;
  int rst_pin;
  int int_pin;
  uint16_t width;
  uint16_t height;
  uint16_t last_x;
  uint16_t last_y;
  bool was_pressed;
} gt911_state_t;

bool driver_input_gt911_init(eos_dev_t *dev) {
  gt911_state_t *st = calloc(1, sizeof(gt911_state_t));
  if (!st) return false;
  dev->state = st;

  int sda_pin = eos_pin_get_no(dev->pins, "sda");
  if (sda_pin < 0) sda_pin = eos_pin_get_no(dev->pins, "sda_io_num");
  if (sda_pin < 0) sda_pin = 7;

  int scl_pin = eos_pin_get_no(dev->pins, "scl");
  if (scl_pin < 0) scl_pin = eos_pin_get_no(dev->pins, "scl_io_num");
  if (scl_pin < 0) scl_pin = 8;

  int rst_pin = eos_pin_get_no(dev->pins, "rst");
  if (rst_pin < 0) rst_pin = eos_pin_get_no(dev->pins, "rst_io_num");

  int int_pin = eos_pin_get_no(dev->pins, "int");
  if (int_pin < 0) int_pin = eos_pin_get_no(dev->pins, "int_io_num");

  int i2c_port = eos_cfg_get_i(dev->cfg, "port", I2C_NUM_0);
  int clk_speed = eos_cfg_get_i(dev->cfg, "clk_speed", 400000);
  uint16_t width = (uint16_t)eos_cfg_get_i(dev->cfg, "width", 480);
  uint16_t height = (uint16_t)eos_cfg_get_i(dev->cfg, "height", 800);
  uint16_t addr = (uint16_t)eos_cfg_get_i(dev->cfg, "addr", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
  bool swap_xy = (bool)eos_cfg_get_i(dev->cfg, "swap_xy", 0);
  bool mirror_x = (bool)eos_cfg_get_i(dev->cfg, "mirror_x", 0);
  bool mirror_y = (bool)eos_cfg_get_i(dev->cfg, "mirror_y", 0);

  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  st->rst_pin = rst_pin;
  st->int_pin = int_pin;
  st->width = width;
  st->height = height;

  // Claim GPIOs
  if (sda_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, sda_pin, dev)) {
    free(st);
    dev->state = NULL;
    return false;
  }
  if (scl_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, scl_pin, dev)) {
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }
  if (rst_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, rst_pin, dev)) {
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    if (scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, scl_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }
  if (int_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, int_pin, dev)) {
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    if (scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, scl_pin, dev);
    if (rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  // Create I2C master bus
  i2c_master_bus_config_t bus_cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .sda_io_num = sda_pin,
      .scl_io_num = scl_pin,
      .i2c_port = i2c_port,
      .glitch_ignore_cnt = 7,
  };
  esp_err_t err = i2c_new_master_bus(&bus_cfg, &st->i2c_bus);
  if (err != ESP_OK) {
    EOS_LOGE("gt911: i2c_new_master_bus failed: %s", esp_err_to_name(err));
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    if (scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, scl_pin, dev);
    if (rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    if (int_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, int_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  // Create panel IO for GT911
  esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  tp_io_cfg.scl_speed_hz = clk_speed;
  if (addr != 0) {
    tp_io_cfg.dev_addr = addr;
  }
  err = esp_lcd_new_panel_io_i2c(st->i2c_bus, &tp_io_cfg, &st->tp_io);
  if (err != ESP_OK) {
    EOS_LOGE("gt911: esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(err));
    i2c_del_master_bus(st->i2c_bus);
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    if (scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, scl_pin, dev);
    if (rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    if (int_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, int_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  // Create GT911 touch instance
  esp_lcd_touch_config_t tp_cfg = {
      .x_max = width,
      .y_max = height,
      .rst_gpio_num = (rst_pin >= 0) ? (gpio_num_t)rst_pin : GPIO_NUM_NC,
      .int_gpio_num = (int_pin >= 0) ? (gpio_num_t)int_pin : GPIO_NUM_NC,
      .levels = {
          .reset = 0,
          .interrupt = 0,
      },
      .flags = {
          .swap_xy = swap_xy,
          .mirror_x = mirror_x,
          .mirror_y = mirror_y,
      },
  };
  err = esp_lcd_touch_new_i2c_gt911(st->tp_io, &tp_cfg, &st->tp);
  if (err != ESP_OK) {
    EOS_LOGE("gt911: esp_lcd_touch_new_i2c_gt911 failed: %s", esp_err_to_name(err));
    esp_lcd_panel_io_del(st->tp_io);
    i2c_del_master_bus(st->i2c_bus);
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    if (scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, scl_pin, dev);
    if (rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    if (int_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, int_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  EOS_LOGI("gt911: touch controller initialized (%ux%u, SDA=%d, SCL=%d)",
           (unsigned)width, (unsigned)height, sda_pin, scl_pin);
  return true;
}

void driver_input_gt911_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  gt911_state_t *st = dev->state;

  if (st->tp) {
    esp_lcd_touch_del(st->tp);
  }
  if (st->tp_io) {
    esp_lcd_panel_io_del(st->tp_io);
  }
  if (st->i2c_bus) {
    i2c_del_master_bus(st->i2c_bus);
  }

  if (st->sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->sda_pin, dev);
  if (st->scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->scl_pin, dev);
  if (st->rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->rst_pin, dev);
  if (st->int_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->int_pin, dev);

  free(st);
  dev->state = NULL;
}

int driver_input_gt911_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  gt911_state_t *st = dev->state;
  if (!st || !st->tp) return -1;

  switch (cmd) {
    case EOS_TOUCH_IOCTL_POLL: {
      eos_input_event_t *ev = va_arg(args, eos_input_event_t *);
      if (!ev) return -1;
      esp_lcd_touch_read_data(st->tp);
      uint16_t x[1] = {0};
      uint16_t y[1] = {0};
      uint16_t strength[1] = {0};
      uint8_t count = 0;
      bool pressed = esp_lcd_touch_get_coordinates(st->tp, x, y, strength, &count, 1);
      if (pressed) {
        st->last_x = x[0];
        st->last_y = y[0];
        st->was_pressed = true;
        ev->type = EOS_INPUT_EVENT_TOUCH;
        ev->x = x[0];
        ev->y = y[0];
        ev->state = 1;
        ev->count = count;
        return 1;
      } else if (st->was_pressed) {
        st->was_pressed = false;
        ev->type = EOS_INPUT_EVENT_TOUCH;
        ev->x = st->last_x;
        ev->y = st->last_y;
        ev->state = 0;
        ev->count = 0;
        return 1;
      }
      return 0;
    }
    case EOS_TOUCH_IOCTL_GET_COORDS: {
      uint16_t *out_x = va_arg(args, uint16_t *);
      uint16_t *out_y = va_arg(args, uint16_t *);
      bool *out_pressed = va_arg(args, bool *);
      esp_lcd_touch_read_data(st->tp);
      uint16_t x[1] = {0};
      uint16_t y[1] = {0};
      uint16_t strength[1] = {0};
      uint8_t count = 0;
      bool pressed = esp_lcd_touch_get_coordinates(st->tp, x, y, strength, &count, 1);
      if (pressed) {
        st->last_x = x[0];
        st->last_y = y[0];
      }
      if (out_x) *out_x = pressed ? x[0] : st->last_x;
      if (out_y) *out_y = pressed ? y[0] : st->last_y;
      if (out_pressed) *out_pressed = pressed;
      return 0;
    }
    default:
      return -1;
  }
}

ssize_t driver_input_gt911_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (size < sizeof(eos_input_event_t) || !dst) {
    errno = EINVAL;
    return -1;
  }
  gt911_state_t *st = dev->state;
  if (!st || !st->tp) {
    errno = EBADF;
    return -1;
  }

  eos_input_event_t *ev = (eos_input_event_t *)dst;
  while (1) {
    esp_lcd_touch_read_data(st->tp);
    uint16_t x[1] = {0};
    uint16_t y[1] = {0};
    uint16_t strength[1] = {0};
    uint8_t count = 0;
    bool pressed = esp_lcd_touch_get_coordinates(st->tp, x, y, strength, &count, 1);
    if (pressed) {
      st->last_x = x[0];
      st->last_y = y[0];
      st->was_pressed = true;
      ev->type = EOS_INPUT_EVENT_TOUCH;
      ev->x = x[0];
      ev->y = y[0];
      ev->state = 1;
      ev->count = count;
      return sizeof(eos_input_event_t);
    } else if (st->was_pressed) {
      st->was_pressed = false;
      ev->type = EOS_INPUT_EVENT_TOUCH;
      ev->x = st->last_x;
      ev->y = st->last_y;
      ev->state = 0;
      ev->count = 0;
      return sizeof(eos_input_event_t);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

EOS_DRV_ATTR eos_drv_t driver_input_gt911 = {
    EOS_DRV_INIT,
    .scope = "input",
    .name = "gt911",
    .devname = "touch",
    .init = driver_input_gt911_init,
    .shutdown = driver_input_gt911_shutdown,
    .read = driver_input_gt911_read,
    .ioctl = driver_input_gt911_ioctl,
};

#endif
