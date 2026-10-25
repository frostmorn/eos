#include "includes.h"
#ifdef EOS_DRV_BUS_I2C_ENABLED

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <errno.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"

#define I2C_MAX_FDS 8
#define I2C_ADDR_MAX 0x7F

typedef struct {
  bool in_use;
  uint16_t target_addr;
  int timeout_ms;
} i2c_fd_ctx_t;

typedef struct {
  int port;
  int sda_pin;
  int scl_pin;
  uint32_t clk_speed;
  bool pullup;
  i2c_master_bus_handle_t i2c_bus;
  SemaphoreHandle_t mutex;
  i2c_master_dev_handle_t dev_handles[128];
  i2c_fd_ctx_t fds[I2C_MAX_FDS];
} i2c_bus_state_t;

static esp_err_t get_or_create_device(i2c_bus_state_t *st, uint16_t addr,
                                      uint32_t speed_hz,
                                      i2c_master_dev_handle_t *out_dev) {
  if (addr > I2C_ADDR_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!st->dev_handles[addr]) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = (speed_hz > 0) ? speed_hz : st->clk_speed,
    };
    esp_err_t err =
        i2c_master_bus_add_device(st->i2c_bus, &dev_cfg, &st->dev_handles[addr]);
    if (err != ESP_OK) {
      return err;
    }
  }
  *out_dev = st->dev_handles[addr];
  return ESP_OK;
}

bool driver_bus_i2c_init(eos_dev_t *dev) {
  i2c_bus_state_t *st = calloc(1, sizeof(i2c_bus_state_t));
  if (!st) {
    return false;
  }
  dev->state = st;

  int sda_pin = eos_pin_get_no(dev->pins, "sda");
  if (sda_pin < 0) sda_pin = eos_pin_get_no(dev->pins, "sda_io_num");
  if (sda_pin < 0) sda_pin = 7;

  int scl_pin = eos_pin_get_no(dev->pins, "scl");
  if (scl_pin < 0) scl_pin = eos_pin_get_no(dev->pins, "scl_io_num");
  if (scl_pin < 0) scl_pin = 8;

  int port = eos_cfg_get_i(dev->cfg, "port", 0);
  int clk_speed = eos_cfg_get_i(dev->cfg, "clk_speed", 400000);
  int glitch_cnt = eos_cfg_get_i(dev->cfg, "glitch_ignore_cnt", 7);
  bool pullup = (bool)eos_cfg_get_i(dev->cfg, "pullup", 0);

  st->port = port;
  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  st->clk_speed = (uint32_t)clk_speed;
  st->pullup = pullup;

  // Claim I2C controller resource in capsmgr
  if (!eos_cap_claim(EOS_CAPS_I2C, port, dev)) {
    EOS_LOGE("i2c: I2C port %d already claimed", port);
    free(st);
    dev->state = NULL;
    return false;
  }

  // Claim SDA and SCL GPIO pins
  if (sda_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, sda_pin, dev)) {
    EOS_LOGE("i2c: SDA GPIO %d already claimed", sda_pin);
    eos_cap_release(EOS_CAPS_I2C, port, dev);
    free(st);
    dev->state = NULL;
    return false;
  }
  if (scl_pin >= 0 && !eos_cap_claim(EOS_CAPS_GPIO, scl_pin, dev)) {
    EOS_LOGE("i2c: SCL GPIO %d already claimed", scl_pin);
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    eos_cap_release(EOS_CAPS_I2C, port, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  st->mutex = xSemaphoreCreateMutex();
  if (!st->mutex) {
    EOS_LOGE("i2c: failed to create bus mutex");
    if (scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, scl_pin, dev);
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    eos_cap_release(EOS_CAPS_I2C, port, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  i2c_master_bus_config_t bus_cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = (i2c_port_num_t)port,
      .scl_io_num = (gpio_num_t)scl_pin,
      .sda_io_num = (gpio_num_t)sda_pin,
      .glitch_ignore_cnt = (uint8_t)glitch_cnt,
      .flags.enable_internal_pullup = pullup ? 1 : 0,
  };

  esp_err_t err = i2c_new_master_bus(&bus_cfg, &st->i2c_bus);
  if (err != ESP_OK) {
    EOS_LOGE("i2c: i2c_new_master_bus failed: %s", esp_err_to_name(err));
    vSemaphoreDelete(st->mutex);
    if (scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, scl_pin, dev);
    if (sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, sda_pin, dev);
    eos_cap_release(EOS_CAPS_I2C, port, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  EOS_LOGI("i2c: master bus %d initialized (SDA=%d, SCL=%d, clk=%lu Hz)", port,
           sda_pin, scl_pin, (unsigned long)st->clk_speed);
  return true;
}

void driver_bus_i2c_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  i2c_bus_state_t *st = dev->state;

  if (st->mutex) {
    xSemaphoreTake(st->mutex, portMAX_DELAY);
  }

  for (int i = 0; i < 128; i++) {
    if (st->dev_handles[i]) {
      i2c_master_bus_rm_device(st->dev_handles[i]);
      st->dev_handles[i] = NULL;
    }
  }

  if (st->i2c_bus) {
    i2c_del_master_bus(st->i2c_bus);
    st->i2c_bus = NULL;
  }

  if (st->mutex) {
    xSemaphoreGive(st->mutex);
    vSemaphoreDelete(st->mutex);
    st->mutex = NULL;
  }

  if (st->scl_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->scl_pin, dev);
  if (st->sda_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->sda_pin, dev);
  eos_cap_release(EOS_CAPS_I2C, st->port, dev);

  free(st);
  dev->state = NULL;
}

bool driver_bus_i2c_attach_req(eos_dev_t *dev, eos_dev_t *child) {
  (void)dev;
  (void)child;
  return true;
}

bool driver_bus_i2c_detach_req(eos_dev_t *dev, eos_dev_t *child) {
  (void)dev;
  (void)child;
  return true;
}

int driver_bus_i2c_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = EBADF;
    return -1;
  }
  i2c_bus_state_t *st = dev->state;

  int slot = -1;
  for (int i = 0; i < I2C_MAX_FDS; i++) {
    if (!st->fds[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    errno = EMFILE;
    return -1;
  }

  uint16_t parsed_addr = 0;
  if (path && *path) {
    const char *p = (*path == '/') ? path + 1 : path;
    if (*p) {
      char *endptr = NULL;
      unsigned long val = strtoul(p, &endptr, 0);
      if (endptr != p && val <= I2C_ADDR_MAX) {
        parsed_addr = (uint16_t)val;
      }
    }
  }

  st->fds[slot].in_use = true;
  st->fds[slot].target_addr = parsed_addr;
  st->fds[slot].timeout_ms = 1000;
  return slot;
}

int driver_bus_i2c_close(eos_dev_t *dev, int fd) {
  if (!dev || !dev->state) {
    errno = EBADF;
    return -1;
  }
  i2c_bus_state_t *st = dev->state;
  if (fd >= 0 && fd < I2C_MAX_FDS) {
    st->fds[fd].in_use = false;
    st->fds[fd].target_addr = 0;
  }
  return 0;
}

ssize_t driver_bus_i2c_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  if (!dev || !dev->state || !dst || size == 0) {
    errno = EINVAL;
    return -1;
  }
  i2c_bus_state_t *st = dev->state;

  uint16_t addr = 0;
  int timeout = 1000;
  if (fd >= 0 && fd < I2C_MAX_FDS && st->fds[fd].in_use) {
    addr = st->fds[fd].target_addr;
    timeout = st->fds[fd].timeout_ms;
  }
  if (addr == 0 || addr > I2C_ADDR_MAX) {
    errno = EDESTADDRREQ;
    return -1;
  }

  if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(timeout)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  i2c_master_dev_handle_t dev_h = NULL;
  esp_err_t err = get_or_create_device(st, addr, st->clk_speed, &dev_h);
  if (err != ESP_OK) {
    xSemaphoreGive(st->mutex);
    errno = EIO;
    return -1;
  }

  err = i2c_master_receive(dev_h, (uint8_t *)dst, size, timeout);
  xSemaphoreGive(st->mutex);

  if (err != ESP_OK) {
    errno = (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
    return -1;
  }
  return (ssize_t)size;
}

ssize_t driver_bus_i2c_write(eos_dev_t *dev, int fd, const void *data,
                             size_t size) {
  if (!dev || !dev->state || !data || size == 0) {
    errno = EINVAL;
    return -1;
  }
  i2c_bus_state_t *st = dev->state;

  uint16_t addr = 0;
  int timeout = 1000;
  if (fd >= 0 && fd < I2C_MAX_FDS && st->fds[fd].in_use) {
    addr = st->fds[fd].target_addr;
    timeout = st->fds[fd].timeout_ms;
  }
  if (addr == 0 || addr > I2C_ADDR_MAX) {
    errno = EDESTADDRREQ;
    return -1;
  }

  if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(timeout)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  i2c_master_dev_handle_t dev_h = NULL;
  esp_err_t err = get_or_create_device(st, addr, st->clk_speed, &dev_h);
  if (err != ESP_OK) {
    xSemaphoreGive(st->mutex);
    errno = EIO;
    return -1;
  }

  err = i2c_master_transmit(dev_h, (const uint8_t *)data, size, timeout);
  xSemaphoreGive(st->mutex);

  if (err != ESP_OK) {
    errno = (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
    return -1;
  }
  return (ssize_t)size;
}

int driver_bus_i2c_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  if (!dev || !dev->state) {
    errno = EBADF;
    return -1;
  }
  i2c_bus_state_t *st = dev->state;

  switch (cmd) {
  case EOS_I2C_IOCTL_GET_BUS_HANDLE: {
    void **out = va_arg(args, void **);
    if (!out) {
      errno = EINVAL;
      return -1;
    }
    *out = (void *)st->i2c_bus;
    return 0;
  }

  case EOS_I2C_IOCTL_SET_SLAVE_ADDR: {
    uint16_t addr = (uint16_t)va_arg(args, int);
    if (addr > I2C_ADDR_MAX) {
      errno = EINVAL;
      return -1;
    }
    if (fd >= 0 && fd < I2C_MAX_FDS && st->fds[fd].in_use) {
      st->fds[fd].target_addr = addr;
      return 0;
    }
    errno = EBADF;
    return -1;
  }

  case EOS_I2C_IOCTL_PROBE: {
    uint16_t addr = (uint16_t)va_arg(args, int);
    int timeout_ms = va_arg(args, int);
    if (timeout_ms <= 0) timeout_ms = 50;
    if (addr > I2C_ADDR_MAX) {
      errno = EINVAL;
      return -1;
    }
    if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(timeout_ms + 100)) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    esp_err_t err = i2c_master_probe(st->i2c_bus, addr, timeout_ms);
    xSemaphoreGive(st->mutex);
    if (err == ESP_OK) {
      return 0;
    }
    errno = (err == ESP_ERR_NOT_FOUND) ? ENXIO : ETIMEDOUT;
    return -1;
  }

  case EOS_I2C_IOCTL_SCAN: {
    eos_i2c_scan_t *scan = va_arg(args, eos_i2c_scan_t *);
    if (!scan) {
      errno = EINVAL;
      return -1;
    }
    scan->count = 0;
    int timeout_ms = (scan->timeout_ms > 0) ? scan->timeout_ms : 20;
    if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
      esp_err_t err = i2c_master_probe(st->i2c_bus, addr, timeout_ms);
      if (err == ESP_OK) {
        if (scan->count < sizeof(scan->addrs)) {
          scan->addrs[scan->count++] = (uint8_t)addr;
        }
      }
    }
    xSemaphoreGive(st->mutex);
    return 0;
  }

  case EOS_I2C_IOCTL_TRANSFER: {
    eos_i2c_msg_t *msg = va_arg(args, eos_i2c_msg_t *);
    if (!msg || msg->addr > I2C_ADDR_MAX) {
      errno = EINVAL;
      return -1;
    }
    int timeout = (msg->timeout_ms > 0) ? msg->timeout_ms : 1000;
    if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(timeout)) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    i2c_master_dev_handle_t dev_h = NULL;
    esp_err_t err =
        get_or_create_device(st, msg->addr, msg->clk_speed_hz, &dev_h);
    if (err != ESP_OK) {
      xSemaphoreGive(st->mutex);
      errno = EIO;
      return -1;
    }
    if (msg->tx_buf && msg->tx_len > 0 && msg->rx_buf && msg->rx_len > 0) {
      err = i2c_master_transmit_receive(dev_h, (const uint8_t *)msg->tx_buf,
                                        msg->tx_len, (uint8_t *)msg->rx_buf,
                                        msg->rx_len, timeout);
    } else if (msg->tx_buf && msg->tx_len > 0) {
      err = i2c_master_transmit(dev_h, (const uint8_t *)msg->tx_buf,
                                msg->tx_len, timeout);
    } else if (msg->rx_buf && msg->rx_len > 0) {
      err = i2c_master_receive(dev_h, (uint8_t *)msg->rx_buf, msg->rx_len,
                               timeout);
    } else {
      err = ESP_ERR_INVALID_ARG;
    }
    xSemaphoreGive(st->mutex);
    if (err != ESP_OK) {
      errno = (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
      return -1;
    }
    return 0;
  }

  case EOS_I2C_IOCTL_TRANSMIT: {
    uint16_t addr = (uint16_t)va_arg(args, int);
    const void *buf = va_arg(args, const void *);
    size_t len = va_arg(args, size_t);
    int timeout = va_arg(args, int);
    if (!buf || len == 0 || addr > I2C_ADDR_MAX) {
      errno = EINVAL;
      return -1;
    }
    if (timeout <= 0) timeout = 1000;
    if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(timeout)) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    i2c_master_dev_handle_t dev_h = NULL;
    esp_err_t err = get_or_create_device(st, addr, st->clk_speed, &dev_h);
    if (err == ESP_OK) {
      err = i2c_master_transmit(dev_h, (const uint8_t *)buf, len, timeout);
    }
    xSemaphoreGive(st->mutex);
    if (err != ESP_OK) {
      errno = (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
      return -1;
    }
    return 0;
  }

  case EOS_I2C_IOCTL_RECEIVE: {
    uint16_t addr = (uint16_t)va_arg(args, int);
    void *buf = va_arg(args, void *);
    size_t len = va_arg(args, size_t);
    int timeout = va_arg(args, int);
    if (!buf || len == 0 || addr > I2C_ADDR_MAX) {
      errno = EINVAL;
      return -1;
    }
    if (timeout <= 0) timeout = 1000;
    if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(timeout)) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    i2c_master_dev_handle_t dev_h = NULL;
    esp_err_t err = get_or_create_device(st, addr, st->clk_speed, &dev_h);
    if (err == ESP_OK) {
      err = i2c_master_receive(dev_h, (uint8_t *)buf, len, timeout);
    }
    xSemaphoreGive(st->mutex);
    if (err != ESP_OK) {
      errno = (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
      return -1;
    }
    return 0;
  }

  case EOS_I2C_IOCTL_WRITE_READ: {
    uint16_t addr = (uint16_t)va_arg(args, int);
    const void *tx = va_arg(args, const void *);
    size_t tx_len = va_arg(args, size_t);
    void *rx = va_arg(args, void *);
    size_t rx_len = va_arg(args, size_t);
    int timeout = va_arg(args, int);
    if (!tx || tx_len == 0 || !rx || rx_len == 0 || addr > I2C_ADDR_MAX) {
      errno = EINVAL;
      return -1;
    }
    if (timeout <= 0) timeout = 1000;
    if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(timeout)) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    i2c_master_dev_handle_t dev_h = NULL;
    esp_err_t err = get_or_create_device(st, addr, st->clk_speed, &dev_h);
    if (err == ESP_OK) {
      err = i2c_master_transmit_receive(dev_h, (const uint8_t *)tx, tx_len,
                                        (uint8_t *)rx, rx_len, timeout);
    }
    xSemaphoreGive(st->mutex);
    if (err != ESP_OK) {
      errno = (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
      return -1;
    }
    return 0;
  }

  case EOS_I2C_IOCTL_SET_FREQ: {
    uint32_t speed = va_arg(args, uint32_t);
    if (speed == 0) {
      errno = EINVAL;
      return -1;
    }
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    st->clk_speed = speed;
    for (int i = 0; i < 128; i++) {
      if (st->dev_handles[i]) {
        i2c_master_bus_rm_device(st->dev_handles[i]);
        st->dev_handles[i] = NULL;
      }
    }
    xSemaphoreGive(st->mutex);
    return 0;
  }

  default:
    errno = ENOSYS;
    return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_bus_i2c = {
    EOS_DRV_INIT,
    .scope = "bus",
    .name = "i2c",
    .devname = "i2c",
    .init = driver_bus_i2c_init,
    .shutdown = driver_bus_i2c_shutdown,
    .attach_req = driver_bus_i2c_attach_req,
    .detach_req = driver_bus_i2c_detach_req,
    .open = driver_bus_i2c_open,
    .close = driver_bus_i2c_close,
    .read = driver_bus_i2c_read,
    .write = driver_bus_i2c_write,
    .ioctl = driver_bus_i2c_ioctl,
};

#endif
