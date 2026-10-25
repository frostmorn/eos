#include "includes.h"
#ifdef EOS_DRV_BUS_I2C_ENABLED

#include <dirent.h>
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
  uint32_t esp_idf_fs_index; // Must be first field for ESP-IDF VFS
  int idx;                   // Address scan index (0x08 to 0x77)
  struct dirent entry;       // Per-instance dirent
} i2c_dir_t;

typedef struct {
  int port;
  int sda_pin;
  int scl_pin;
  uint32_t clk_speed;
  int glitch_cnt;
  bool pullup;
  bool pins_claimed;
  int open_refcount;
  int attached_count;
  i2c_master_bus_handle_t i2c_bus;
  SemaphoreHandle_t mutex;
  i2c_master_dev_handle_t dev_handles[128];
  i2c_fd_ctx_t fds[I2C_MAX_FDS];
} i2c_bus_state_t;

static bool driver_bus_i2c_has_attached_devices(eos_dev_t *dev,
                                                i2c_bus_state_t *st) {
  if (dev && dev->child != NULL) return true;
  if (st && st->attached_count > 0) return true;
  return false;
}

static esp_err_t bus_activate_locked(eos_dev_t *dev, i2c_bus_state_t *st) {
  if (st->i2c_bus != NULL) {
    return ESP_OK;
  }
  if (st->sda_pin < 0 || st->scl_pin < 0) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!eos_cap_claim(EOS_CAPS_GPIO, st->sda_pin, dev)) {
    EOS_LOGE("i2c: failed to claim SDA GPIO %d", st->sda_pin);
    return ESP_ERR_NOT_FOUND;
  }
  if (!eos_cap_claim(EOS_CAPS_GPIO, st->scl_pin, dev)) {
    EOS_LOGE("i2c: failed to claim SCL GPIO %d", st->scl_pin);
    eos_cap_release(EOS_CAPS_GPIO, st->sda_pin, dev);
    return ESP_ERR_NOT_FOUND;
  }

  i2c_master_bus_config_t bus_cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = (i2c_port_num_t)st->port,
      .scl_io_num = (gpio_num_t)st->scl_pin,
      .sda_io_num = (gpio_num_t)st->sda_pin,
      .glitch_ignore_cnt = (uint8_t)st->glitch_cnt,
      .flags.enable_internal_pullup = st->pullup ? 1 : 0,
  };

  esp_err_t err = i2c_new_master_bus(&bus_cfg, &st->i2c_bus);
  if (err != ESP_OK) {
    EOS_LOGE("i2c: i2c_new_master_bus failed: %s", esp_err_to_name(err));
    eos_cap_release(EOS_CAPS_GPIO, st->scl_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->sda_pin, dev);
    return err;
  }

  st->pins_claimed = true;
  EOS_LOGI("i2c: bus activated (port=%d, SDA=%d, SCL=%d, clk=%lu Hz)",
           st->port, st->sda_pin, st->scl_pin, (unsigned long)st->clk_speed);
  return ESP_OK;
}

static void bus_deactivate_locked(eos_dev_t *dev, i2c_bus_state_t *st) {
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

  if (st->pins_claimed) {
    if (st->scl_pin >= 0) {
      eos_cap_release(EOS_CAPS_GPIO, st->scl_pin, dev);
      gpio_reset_pin((gpio_num_t)st->scl_pin);
    }
    if (st->sda_pin >= 0) {
      eos_cap_release(EOS_CAPS_GPIO, st->sda_pin, dev);
      gpio_reset_pin((gpio_num_t)st->sda_pin);
    }
    st->pins_claimed = false;
    EOS_LOGI("i2c: bus deactivated, released pins (SDA=%d, SCL=%d)",
             st->sda_pin, st->scl_pin);
  }
}

static int reconfigure_pins_locked(eos_dev_t *dev, int new_sda, int new_scl) {
  i2c_bus_state_t *st = dev->state;

  if (driver_bus_i2c_has_attached_devices(dev, st)) {
    EOS_LOGW("i2c: cannot reconfigure pins while devices are attached");
    errno = EBUSY;
    return -1;
  }

  if (new_sda < -1 || (new_sda >= 0 && !GPIO_IS_VALID_GPIO(new_sda))) {
    errno = EINVAL;
    return -1;
  }
  if (new_scl < -1 || (new_scl >= 0 && !GPIO_IS_VALID_GPIO(new_scl))) {
    errno = EINVAL;
    return -1;
  }
  if (new_sda >= 0 && new_scl >= 0 && new_sda == new_scl) {
    errno = EINVAL;
    return -1;
  }

  int old_sda = st->sda_pin;
  int old_scl = st->scl_pin;

  if (new_sda == old_sda && new_scl == old_scl) {
    return 0;
  }

  if (st->pins_claimed) {
    if (new_sda != old_sda && new_sda >= 0) {
      if (!eos_cap_claim(EOS_CAPS_GPIO, new_sda, dev)) {
        EOS_LOGE("i2c: failed to claim new SDA GPIO %d", new_sda);
        errno = EBUSY;
        return -1;
      }
    }
    if (new_scl != old_scl && new_scl >= 0) {
      if (!eos_cap_claim(EOS_CAPS_GPIO, new_scl, dev)) {
        EOS_LOGE("i2c: failed to claim new SCL GPIO %d", new_scl);
        if (new_sda != old_sda && new_sda >= 0) {
          eos_cap_release(EOS_CAPS_GPIO, new_sda, dev);
        }
        errno = EBUSY;
        return -1;
      }
    }

    // Teardown existing bus
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

    // Release old pins and restore GPIO function
    if (new_sda != old_sda && old_sda >= 0) {
      eos_cap_release(EOS_CAPS_GPIO, old_sda, dev);
      gpio_reset_pin((gpio_num_t)old_sda);
    }
    if (new_scl != old_scl && old_scl >= 0) {
      eos_cap_release(EOS_CAPS_GPIO, old_scl, dev);
      gpio_reset_pin((gpio_num_t)old_scl);
    }

    st->sda_pin = new_sda;
    st->scl_pin = new_scl;

    if (new_sda >= 0 && new_scl >= 0) {
      i2c_master_bus_config_t bus_cfg = {
          .clk_source = I2C_CLK_SRC_DEFAULT,
          .i2c_port = (i2c_port_num_t)st->port,
          .scl_io_num = (gpio_num_t)st->scl_pin,
          .sda_io_num = (gpio_num_t)st->sda_pin,
          .glitch_ignore_cnt = (uint8_t)st->glitch_cnt,
          .flags.enable_internal_pullup = st->pullup ? 1 : 0,
      };
      esp_err_t err = i2c_new_master_bus(&bus_cfg, &st->i2c_bus);
      if (err != ESP_OK) {
        EOS_LOGE("i2c: failed to recreate master bus: %s", esp_err_to_name(err));
        eos_cap_release(EOS_CAPS_GPIO, new_scl, dev);
        gpio_reset_pin((gpio_num_t)new_scl);
        eos_cap_release(EOS_CAPS_GPIO, new_sda, dev);
        gpio_reset_pin((gpio_num_t)new_sda);
        st->pins_claimed = false;
        errno = EIO;
        return -1;
      }
      st->pins_claimed = true;
      EOS_LOGI("i2c: bus reconfigured (SDA=%d, SCL=%d)", new_sda, new_scl);
    } else {
      st->pins_claimed = (new_sda >= 0 || new_scl >= 0);
      EOS_LOGI("i2c: released pin(s) (SDA=%d, SCL=%d)", new_sda, new_scl);
    }
  } else {
    // Bus idle / pins not claimed
    st->sda_pin = new_sda;
    st->scl_pin = new_scl;
    EOS_LOGI("i2c: updated unactive pin configuration (SDA=%d, SCL=%d)",
             new_sda, new_scl);
  }

  return 0;
}

static esp_err_t get_or_create_device(eos_dev_t *dev, i2c_bus_state_t *st,
                                      uint16_t addr, uint32_t speed_hz,
                                      i2c_master_dev_handle_t *out_dev) {
  if (addr > I2C_ADDR_MAX) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!st->i2c_bus) {
    if (st->sda_pin >= 0 && st->scl_pin >= 0) {
      esp_err_t err = bus_activate_locked(dev, st);
      if (err != ESP_OK) return err;
    } else {
      return ESP_ERR_INVALID_STATE;
    }
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

  int scl_pin = eos_pin_get_no(dev->pins, "scl");
  if (scl_pin < 0) scl_pin = eos_pin_get_no(dev->pins, "scl_io_num");

  int port = eos_cfg_get_i(dev->cfg, "port", 0);
  int clk_speed = eos_cfg_get_i(dev->cfg, "clk_speed", 400000);
  int glitch_cnt = eos_cfg_get_i(dev->cfg, "glitch_ignore_cnt", 7);
  bool pullup = (bool)eos_cfg_get_i(dev->cfg, "pullup", 0);

  st->port = port;
  st->sda_pin = sda_pin;
  st->scl_pin = scl_pin;
  st->clk_speed = (uint32_t)clk_speed;
  st->glitch_cnt = glitch_cnt;
  st->pullup = pullup;

  // Claim I2C controller resource in capsmgr
  if (!eos_cap_claim(EOS_CAPS_I2C, port, dev)) {
    EOS_LOGE("i2c: I2C port %d already claimed", port);
    free(st);
    dev->state = NULL;
    return false;
  }

  st->mutex = xSemaphoreCreateMutex();
  if (!st->mutex) {
    EOS_LOGE("i2c: failed to create bus mutex");
    eos_cap_release(EOS_CAPS_I2C, port, dev);
    free(st);
    dev->state = NULL;
    return false;
  }

  EOS_LOGI("i2c: initialized bus device on port %d (SDA=%d, SCL=%d, clk=%lu Hz)",
           port, sda_pin, scl_pin, (unsigned long)st->clk_speed);
  return true;
}

void driver_bus_i2c_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  i2c_bus_state_t *st = dev->state;

  if (st->mutex) {
    xSemaphoreTake(st->mutex, portMAX_DELAY);
  }

  bus_deactivate_locked(dev, st);

  if (st->mutex) {
    xSemaphoreGive(st->mutex);
    vSemaphoreDelete(st->mutex);
    st->mutex = NULL;
  }

  eos_cap_release(EOS_CAPS_I2C, st->port, dev);

  free(st);
  dev->state = NULL;
}

bool driver_bus_i2c_attach_req(eos_dev_t *dev, eos_dev_t *child) {
  (void)child;
  if (!dev || !dev->state) return false;
  i2c_bus_state_t *st = dev->state;

  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (!st->i2c_bus) {
    if (st->sda_pin < 0 || st->scl_pin < 0) {
      xSemaphoreGive(st->mutex);
      EOS_LOGW("i2c: cannot attach child device, bus pins not configured");
      return false;
    }
    esp_err_t err = bus_activate_locked(dev, st);
    if (err != ESP_OK) {
      xSemaphoreGive(st->mutex);
      return false;
    }
  }

  st->attached_count++;
  xSemaphoreGive(st->mutex);
  return true;
}

bool driver_bus_i2c_detach_req(eos_dev_t *dev, eos_dev_t *child) {
  (void)child;
  if (!dev || !dev->state) return false;
  i2c_bus_state_t *st = dev->state;

  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (st->attached_count > 0) {
    st->attached_count--;
  }

  if (st->attached_count == 0 &&
      (dev->child == NULL || dev->child == child || dev->child->next == NULL) &&
      st->open_refcount == 0) {
    bus_deactivate_locked(dev, st);
  }

  xSemaphoreGive(st->mutex);
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

  uint16_t parsed_addr = 0;
  if (path && *path) {
    const char *lastToken = strrchr(path, '/');
    if (lastToken)
      lastToken++;
    else
      lastToken = (char *)path;

    if (*lastToken) {
      char *endptr = NULL;
      unsigned long val = strtoul(lastToken, &endptr, 0);
      if (endptr != lastToken && val <= I2C_ADDR_MAX) {
        parsed_addr = (uint16_t)val;
      } else {
        errno = ENOENT;
        return -1;
      }
    }
  }

  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  int slot = -1;
  for (int i = 0; i < I2C_MAX_FDS; i++) {
    if (!st->fds[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    xSemaphoreGive(st->mutex);
    errno = EMFILE;
    return -1;
  }

  // Activate bus if pins configured and not active
  if (!st->i2c_bus && st->sda_pin >= 0 && st->scl_pin >= 0) {
    esp_err_t err = bus_activate_locked(dev, st);
    if (err != ESP_OK) {
      xSemaphoreGive(st->mutex);
      errno = (err == ESP_ERR_NOT_FOUND) ? EBUSY : EIO;
      return -1;
    }
  }

  st->fds[slot].in_use = true;
  st->fds[slot].target_addr = parsed_addr;
  st->fds[slot].timeout_ms = 1000;
  st->open_refcount++;
  xSemaphoreGive(st->mutex);

  return slot;
}

int driver_bus_i2c_close(eos_dev_t *dev, int fd) {
  if (!dev || !dev->state) {
    errno = EBADF;
    return -1;
  }
  i2c_bus_state_t *st = dev->state;

  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  if (fd >= 0 && fd < I2C_MAX_FDS && st->fds[fd].in_use) {
    st->fds[fd].in_use = false;
    st->fds[fd].target_addr = 0;
    if (st->open_refcount > 0) {
      st->open_refcount--;
    }
  }

  // Automatically release pins on file descriptors closed, unless device attached
  if (st->open_refcount == 0 && !driver_bus_i2c_has_attached_devices(dev, st)) {
    bus_deactivate_locked(dev, st);
  }

  xSemaphoreGive(st->mutex);
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
  esp_err_t err = get_or_create_device(dev, st, addr, st->clk_speed, &dev_h);
  if (err != ESP_OK) {
    xSemaphoreGive(st->mutex);
    errno = (err == ESP_ERR_INVALID_STATE) ? ENODEV : EIO;
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
  esp_err_t err = get_or_create_device(dev, st, addr, st->clk_speed, &dev_h);
  if (err != ESP_OK) {
    xSemaphoreGive(st->mutex);
    errno = (err == ESP_ERR_INVALID_STATE) ? ENODEV : EIO;
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
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    if (!st->i2c_bus && st->sda_pin >= 0 && st->scl_pin >= 0) {
      bus_activate_locked(dev, st);
    }
    if (!st->i2c_bus) {
      xSemaphoreGive(st->mutex);
      errno = ENODEV;
      return -1;
    }
    *out = (void *)st->i2c_bus;
    xSemaphoreGive(st->mutex);
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
    if (!st->i2c_bus && st->sda_pin >= 0 && st->scl_pin >= 0) {
      bus_activate_locked(dev, st);
    }
    if (!st->i2c_bus) {
      xSemaphoreGive(st->mutex);
      errno = ENODEV;
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
    if (!st->i2c_bus && st->sda_pin >= 0 && st->scl_pin >= 0) {
      bus_activate_locked(dev, st);
    }
    if (!st->i2c_bus) {
      xSemaphoreGive(st->mutex);
      errno = ENODEV;
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
        get_or_create_device(dev, st, msg->addr, msg->clk_speed_hz, &dev_h);
    if (err != ESP_OK) {
      xSemaphoreGive(st->mutex);
      errno = (err == ESP_ERR_INVALID_STATE) ? ENODEV : EIO;
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
    if (addr == 0 && fd >= 0 && fd < I2C_MAX_FDS && st->fds[fd].in_use) {
      addr = st->fds[fd].target_addr;
    }
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
    esp_err_t err = get_or_create_device(dev, st, addr, st->clk_speed, &dev_h);
    if (err == ESP_OK) {
      err = i2c_master_transmit(dev_h, (const uint8_t *)buf, len, timeout);
    }
    xSemaphoreGive(st->mutex);
    if (err != ESP_OK) {
      errno = (err == ESP_ERR_INVALID_STATE)
                  ? ENODEV
                  : (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
      return -1;
    }
    return 0;
  }

  case EOS_I2C_IOCTL_RECEIVE: {
    uint16_t addr = (uint16_t)va_arg(args, int);
    void *buf = va_arg(args, void *);
    size_t len = va_arg(args, size_t);
    int timeout = va_arg(args, int);
    if (addr == 0 && fd >= 0 && fd < I2C_MAX_FDS && st->fds[fd].in_use) {
      addr = st->fds[fd].target_addr;
    }
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
    esp_err_t err = get_or_create_device(dev, st, addr, st->clk_speed, &dev_h);
    if (err == ESP_OK) {
      err = i2c_master_receive(dev_h, (uint8_t *)buf, len, timeout);
    }
    xSemaphoreGive(st->mutex);
    if (err != ESP_OK) {
      errno = (err == ESP_ERR_INVALID_STATE)
                  ? ENODEV
                  : (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
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
    if (addr == 0 && fd >= 0 && fd < I2C_MAX_FDS && st->fds[fd].in_use) {
      addr = st->fds[fd].target_addr;
    }
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
    esp_err_t err = get_or_create_device(dev, st, addr, st->clk_speed, &dev_h);
    if (err == ESP_OK) {
      err = i2c_master_transmit_receive(dev_h, (const uint8_t *)tx, tx_len,
                                        (uint8_t *)rx, rx_len, timeout);
    }
    xSemaphoreGive(st->mutex);
    if (err != ESP_OK) {
      errno = (err == ESP_ERR_INVALID_STATE)
                  ? ENODEV
                  : (err == ESP_ERR_TIMEOUT) ? ETIMEDOUT : EIO;
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

  case EOS_I2C_IOCTL_SET_SDA: {
    int pin = va_arg(args, int);
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    int rc = reconfigure_pins_locked(dev, pin, st->scl_pin);
    xSemaphoreGive(st->mutex);
    return rc;
  }

  case EOS_I2C_IOCTL_SET_SCL: {
    int pin = va_arg(args, int);
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    int rc = reconfigure_pins_locked(dev, st->sda_pin, pin);
    xSemaphoreGive(st->mutex);
    return rc;
  }

  case EOS_I2C_IOCTL_SET_PINS: {
    int sda = va_arg(args, int);
    int scl = va_arg(args, int);
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    int rc = reconfigure_pins_locked(dev, sda, scl);
    xSemaphoreGive(st->mutex);
    return rc;
  }

  case EOS_I2C_IOCTL_GET_PINS: {
    int *out_sda = va_arg(args, int *);
    int *out_scl = va_arg(args, int *);
    if (!out_sda || !out_scl) {
      errno = EINVAL;
      return -1;
    }
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
      errno = EBUSY;
      return -1;
    }
    *out_sda = st->sda_pin;
    *out_scl = st->scl_pin;
    xSemaphoreGive(st->mutex);
    return 0;
  }

  default:
    errno = ENOSYS;
    return -1;
  }
}

DIR *driver_bus_i2c_opendir(eos_dev_t *dev, const char *path) {
  (void)path;
  if (!dev || !dev->state) {
    errno = EBADF;
    return NULL;
  }
  i2c_bus_state_t *st = dev->state;

  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    errno = EBUSY;
    return NULL;
  }

  if (!st->i2c_bus && st->sda_pin >= 0 && st->scl_pin >= 0) {
    esp_err_t err = bus_activate_locked(dev, st);
    if (err != ESP_OK) {
      xSemaphoreGive(st->mutex);
      errno = (err == ESP_ERR_NOT_FOUND) ? EBUSY : EIO;
      return NULL;
    }
  }

  st->open_refcount++;
  xSemaphoreGive(st->mutex);

  i2c_dir_t *dir = calloc(1, sizeof(i2c_dir_t));
  if (!dir) {
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) == pdTRUE) {
      if (st->open_refcount > 0) st->open_refcount--;
      if (st->open_refcount == 0 &&
          !driver_bus_i2c_has_attached_devices(dev, st)) {
        bus_deactivate_locked(dev, st);
      }
      xSemaphoreGive(st->mutex);
    }
    errno = ENOMEM;
    return NULL;
  }
  dir->idx = 0x08; // Start of standard 7-bit I2C device address range
  return (DIR *)dir;
}

struct dirent *driver_bus_i2c_readdir(eos_dev_t *dev, DIR *pdir) {
  i2c_dir_t *dir = (i2c_dir_t *)pdir;
  i2c_bus_state_t *st = dev ? dev->state : NULL;
  if (!dir || !st) {
    errno = EBADF;
    return NULL;
  }

  while (dir->idx <= 0x77) {
    int addr = dir->idx;
    dir->idx++;

    bool present = false;
    if (st->dev_handles[addr] != NULL) {
      present = true;
    } else {
      if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (!st->i2c_bus && st->sda_pin >= 0 && st->scl_pin >= 0) {
          bus_activate_locked(dev, st);
        }
        if (st->i2c_bus &&
            i2c_master_probe(st->i2c_bus, (uint16_t)addr, 20) == ESP_OK) {
          present = true;
        }
        xSemaphoreGive(st->mutex);
      }
    }

    if (present) {
      memset(&dir->entry, 0, sizeof(dir->entry));
      dir->entry.d_type = DT_CHR;
      dir->entry.d_ino = (ino_t)addr;
      snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "0x%02x", addr);
      return &dir->entry;
    }
  }

  return NULL;
}

void driver_bus_i2c_seekdir(eos_dev_t *dev, DIR *pdir, long offset) {
  (void)dev;
  i2c_dir_t *dir = (i2c_dir_t *)pdir;
  if (dir) {
    dir->idx = (int)offset;
  }
}

long driver_bus_i2c_telldir(eos_dev_t *dev, DIR *pdir) {
  (void)dev;
  i2c_dir_t *dir = (i2c_dir_t *)pdir;
  return dir ? dir->idx : -1;
}

int driver_bus_i2c_closedir(eos_dev_t *dev, DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return -1;
  }
  free(pdir);

  if (dev && dev->state) {
    i2c_bus_state_t *st = dev->state;
    if (xSemaphoreTake(st->mutex, portMAX_DELAY) == pdTRUE) {
      if (st->open_refcount > 0) {
        st->open_refcount--;
      }
      if (st->open_refcount == 0 &&
          !driver_bus_i2c_has_attached_devices(dev, st)) {
        bus_deactivate_locked(dev, st);
      }
      xSemaphoreGive(st->mutex);
    }
  }
  return 0;
}

EOS_DRV_ATTR eos_drv_t driver_bus_i2c = {
    EOS_DRV_INIT,
    .flags = EOS_DRV_FLAG_NO_INDEX,
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
    .opendir = driver_bus_i2c_opendir,
    .readdir = driver_bus_i2c_readdir,
    .seekdir = driver_bus_i2c_seekdir,
    .telldir = driver_bus_i2c_telldir,
    .closedir = driver_bus_i2c_closedir,
};

#endif
