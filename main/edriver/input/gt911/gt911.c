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
  esp_lcd_panel_io_handle_t tp_io;
  esp_lcd_touch_handle_t tp;
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

  int rst_pin = eos_pin_get_no(dev->pins, "rst");
  if (rst_pin < 0) rst_pin = eos_pin_get_no(dev->pins, "rst_io_num");

  int int_pin = eos_pin_get_no(dev->pins, "int");
  if (int_pin < 0) int_pin = eos_pin_get_no(dev->pins, "int_io_num");

  int clk_speed = eos_cfg_get_i(dev->cfg, "clk_speed", 400000);
  uint16_t width = (uint16_t)eos_cfg_get_i(dev->cfg, "width", 480);
  uint16_t height = (uint16_t)eos_cfg_get_i(dev->cfg, "height", 800);
  uint16_t addr = (uint16_t)eos_cfg_get_i(dev->cfg, "addr", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
  bool swap_xy = (bool)eos_cfg_get_i(dev->cfg, "swap_xy", 0);
  bool mirror_x = (bool)eos_cfg_get_i(dev->cfg, "mirror_x", 0);
  bool mirror_y = (bool)eos_cfg_get_i(dev->cfg, "mirror_y", 0);

  st->rst_pin = rst_pin;
  st->int_pin = int_pin;
  st->width = width;
  st->height = height;

  // Obtain I2C master bus handle from parent I2C bus driver
  if (!dev->parent) {
    EOS_LOGE("gt911: no parent I2C bus device attached");
    free(st);
    dev->state = NULL;
    return false;
  }

  i2c_master_bus_handle_t bus_handle = NULL;
  if (eos_dev_ioctl_call(dev->parent, EOS_DRV_FD, EOS_I2C_IOCTL_GET_BUS_HANDLE,
                         &bus_handle) != 0 ||
      !bus_handle) {
    EOS_LOGE("gt911: failed to query I2C bus handle from parent");
    free(st);
    dev->state = NULL;
    return false;
  }

  // Probe touch controller on the I2C bus
  if (eos_dev_ioctl_call(dev->parent, EOS_DRV_FD, EOS_I2C_IOCTL_PROBE, addr,
                         50) == 0) {
    EOS_LOGI("gt911: detected device at I2C address 0x%02X", (unsigned)addr);
  } else {
    EOS_LOGW("gt911: probe at I2C address 0x%02X returned NACK (proceeding with init)",
             (unsigned)addr);
  }

  // Claim reset and interrupt GPIOs if configured
  if (rst_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, rst_pin, dev)) {
    free(st);
    dev->state = NULL;
    return false;
  }
  if (int_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, int_pin, dev)) {
    if (rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  // Create panel IO for GT911 on the shared I2C bus
  esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  tp_io_cfg.scl_speed_hz = clk_speed;
  if (addr != 0) {
    tp_io_cfg.dev_addr = addr;
  }
  esp_err_t err = esp_lcd_new_panel_io_i2c(bus_handle, &tp_io_cfg, &st->tp_io);
  if (err != ESP_OK) {
    EOS_LOGE("gt911: esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(err));
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
    if (rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, rst_pin, dev);
    if (int_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, int_pin, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  EOS_LOGI("gt911: touch controller initialized via I2C bus (%ux%u, addr=0x%02X)",
           (unsigned)width, (unsigned)height, (unsigned)addr);
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

int driver_input_gt911_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)dev;
  (void)path;
  (void)flags;
  (void)mode;
  return 0;
}

int driver_input_gt911_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

EOS_DRV_ATTR eos_drv_t driver_input_gt911 = {
    EOS_DRV_INIT,
    .scope = "input",
    .name = "gt911",
    .devname = "touch",
    .init = driver_input_gt911_init,
    .shutdown = driver_input_gt911_shutdown,
    .open = driver_input_gt911_open,
    .close = driver_input_gt911_close,
    .read = driver_input_gt911_read,
    .ioctl = driver_input_gt911_ioctl,
};

#endif
