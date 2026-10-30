#include "includes.h"
#ifdef EOS_DRV_BUS_ADC_ENABLED

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <soc/soc_caps.h>

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"

#ifndef SOC_GPIO_PIN_COUNT
#define SOC_GPIO_PIN_COUNT 49
#endif

// ── State ─────────────────────────────────────────────────────

typedef struct {
  adc_oneshot_unit_handle_t unit1;
  adc_oneshot_unit_handle_t unit2;
  adc_cali_handle_t cali[SOC_GPIO_PIN_COUNT];
  bool chan_configured[SOC_GPIO_PIN_COUNT];
  bool pin_claimed[SOC_GPIO_PIN_COUNT];
  int32_t default_pin;
  int32_t read_offset;
  SemaphoreHandle_t lock;
} adc_bus_state_t;

// ── Helper: configure channel on demand ───────────────────────

static bool adc_ensure_pin(adc_bus_state_t *st, eos_dev_t *dev, int32_t pin) {
  if (!st || pin < 0 || pin >= SOC_GPIO_PIN_COUNT) {
    return false;
  }

  if (st->chan_configured[pin]) {
    return true;
  }

  adc_unit_t unit_id;
  adc_channel_t channel;
  esp_err_t err = adc_oneshot_io_to_channel(pin, &unit_id, &channel);
  if (err != ESP_OK) {
    EOS_LOGE("adc: GPIO %" PRId32 " is not a valid ADC pin: %s", pin,
             esp_err_to_name(err));
    return false;
  }

  // Claim GPIO capability if free
  if (!st->pin_claimed[pin]) {
    if (eos_cap_claim(EOS_CAPS_GPIO, pin, dev)) {
      st->pin_claimed[pin] = true;
    }
  }

  // Ensure unit handle
  adc_oneshot_unit_handle_t *u_handle =
      (unit_id == ADC_UNIT_1) ? &st->unit1 : &st->unit2;

  if (!*u_handle) {
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unit_id,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&init_cfg, u_handle);
    if (err != ESP_OK) {
      EOS_LOGE("adc: failed to init ADC unit %d: %s", (int)unit_id,
               esp_err_to_name(err));
      return false;
    }
  }

  // Configure channel (12dB attenuation for ~0..3100mV range)
  adc_oneshot_chan_cfg_t chan_cfg = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_12,
  };
  err = adc_oneshot_config_channel(*u_handle, channel, &chan_cfg);
  if (err != ESP_OK) {
    EOS_LOGE("adc: failed to config channel for GPIO %" PRId32 ": %s", pin,
             esp_err_to_name(err));
    return false;
  }

  // Set up calibration scheme if supported
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cali_cfg = {
      .unit_id = unit_id,
      .chan = channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &st->cali[pin]);
  if (err != ESP_OK) {
    EOS_LOGW("adc: curve fitting cali failed for GPIO %" PRId32
             " (using fallback): %s",
             pin, esp_err_to_name(err));
    st->cali[pin] = NULL;
  }
#endif

  st->chan_configured[pin] = true;
  return true;
}

// ── Read helper ───────────────────────────────────────────────

static bool adc_read_pin(adc_bus_state_t *st, eos_dev_t *dev, int32_t pin,
                        int32_t *out_raw, int32_t *out_mv) {
  if (!st || !adc_ensure_pin(st, dev, pin)) {
    return false;
  }

  adc_unit_t unit_id;
  adc_channel_t channel;
  if (adc_oneshot_io_to_channel(pin, &unit_id, &channel) != ESP_OK) {
    return false;
  }

  adc_oneshot_unit_handle_t u_handle =
      (unit_id == ADC_UNIT_1) ? st->unit1 : st->unit2;
  if (!u_handle) {
    return false;
  }

  int raw_val = 0;
  esp_err_t err = adc_oneshot_read(u_handle, channel, &raw_val);
  if (err != ESP_OK) {
    EOS_LOGE("adc: read error on GPIO %" PRId32 ": %s", pin,
             esp_err_to_name(err));
    return false;
  }

  if (out_raw) {
    *out_raw = raw_val;
  }

  if (out_mv) {
    int mv_val = 0;
    if (st->cali[pin]) {
      err = adc_cali_raw_to_voltage(st->cali[pin], raw_val, &mv_val);
      if (err != ESP_OK) {
        mv_val = (raw_val * 3100) / 4095;
      }
    } else {
      mv_val = (raw_val * 3100) / 4095;
    }
    *out_mv = mv_val;
  }

  return true;
}

// ── Driver Lifecycle ──────────────────────────────────────────

static bool driver_bus_adc_init(eos_dev_t *dev) {
  if (!dev)
    return false;

  adc_bus_state_t *st = calloc(1, sizeof(adc_bus_state_t));
  if (!st)
    return false;

  st->lock = xSemaphoreCreateMutex();
  if (!st->lock) {
    free(st);
    return false;
  }

  // Lilka default battery ADC pin is GPIO 3
  st->default_pin = (int32_t)eos_cfg_get_i(dev->cfg, "pin", 3);
  if (st->default_pin < 0 || st->default_pin >= SOC_GPIO_PIN_COUNT) {
    st->default_pin = 3;
  }

  dev->state = st;
  EOS_LOGI("adc: initialized bus driver (default_pin=%" PRId32 ")",
           st->default_pin);
  return true;
}

static void driver_bus_adc_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state)
    return;

  adc_bus_state_t *st = dev->state;

  if (st->lock) {
    xSemaphoreTake(st->lock, portMAX_DELAY);
  }

  for (int i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (st->cali[i]) {
      adc_cali_delete_scheme_curve_fitting(st->cali[i]);
      st->cali[i] = NULL;
    }
#endif
    if (st->pin_claimed[i]) {
      eos_cap_release(EOS_CAPS_GPIO, i, dev);
      st->pin_claimed[i] = false;
    }
  }

  if (st->unit1) {
    adc_oneshot_del_unit(st->unit1);
    st->unit1 = NULL;
  }
  if (st->unit2) {
    adc_oneshot_del_unit(st->unit2);
    st->unit2 = NULL;
  }

  if (st->lock) {
    xSemaphoreGive(st->lock);
    vSemaphoreDelete(st->lock);
    st->lock = NULL;
  }

  free(st);
  dev->state = NULL;
}

// ── VFS Operations ────────────────────────────────────────────

static int driver_bus_adc_open(eos_dev_t *dev, const char *path, int flags,
                               int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state)
    return -1;
  adc_bus_state_t *st = dev->state;
  st->read_offset = 0;
  return 0;
}

static int driver_bus_adc_close(eos_dev_t *dev, int fd) {
  (void)fd;
  if (!dev || !dev->state)
    return -1;
  adc_bus_state_t *st = dev->state;
  st->read_offset = 0;
  return 0;
}

static ssize_t driver_bus_adc_read(eos_dev_t *dev, int fd, void *dst,
                                   size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst || size == 0) {
    errno = EINVAL;
    return -1;
  }

  adc_bus_state_t *st = dev->state;
  if (xSemaphoreTake(st->lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  if (st->read_offset > 0) {
    xSemaphoreGive(st->lock);
    return 0; // EOF for cat /dev/adc0
  }

  int32_t raw = 0;
  int32_t mv = 0;
  bool ok = adc_read_pin(st, dev, st->default_pin, &raw, &mv);

  if (!ok) {
    xSemaphoreGive(st->lock);
    errno = EIO;
    return -1;
  }

  char buf[64];
  int n = snprintf(buf, sizeof(buf), "pin: %" PRId32 "\nraw: %" PRId32 "\nmv: %" PRId32 "\n",
                   st->default_pin, raw, mv);
  if (n <= 0) {
    xSemaphoreGive(st->lock);
    return 0;
  }

  size_t to_copy = ((size_t)n < size) ? (size_t)n : size;
  memcpy(dst, buf, to_copy);
  st->read_offset += (int32_t)to_copy;
  xSemaphoreGive(st->lock);
  return (ssize_t)to_copy;
}

static int driver_bus_adc_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }

  adc_bus_state_t *st = dev->state;
  if (xSemaphoreTake(st->lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  int ret = 0;
  switch (cmd) {
  case EOS_ADC_IOCTL_READ_RAW: {
    eos_adc_reading_t *r = va_arg(args, eos_adc_reading_t *);
    if (!r) {
      errno = EINVAL;
      ret = -1;
    } else {
      int32_t raw = 0;
      if (!adc_read_pin(st, dev, (int32_t)r->pin, &raw, NULL)) {
        errno = EIO;
        ret = -1;
      } else {
        r->raw = raw;
      }
    }
    break;
  }
  case EOS_ADC_IOCTL_READ_VOLTAGE: {
    eos_adc_reading_t *r = va_arg(args, eos_adc_reading_t *);
    if (!r) {
      errno = EINVAL;
      ret = -1;
    } else {
      int32_t raw = 0;
      int32_t mv = 0;
      if (!adc_read_pin(st, dev, (int32_t)r->pin, &raw, &mv)) {
        errno = EIO;
        ret = -1;
      } else {
        r->raw = raw;
        r->voltage_mv = mv;
      }
    }
    break;
  }
  default:
    errno = ENOTTY;
    ret = -1;
    break;
  }

  xSemaphoreGive(st->lock);
  return ret;
}

// ── Driver Descriptor ─────────────────────────────────────────

EOS_DRV_ATTR eos_drv_t driver_bus_adc = {
    EOS_DRV_INIT,
    .scope = "bus",
    .name = "adc",
    .devname = "adc",
    .init = driver_bus_adc_init,
    .open = driver_bus_adc_open,
    .close = driver_bus_adc_close,
    .read = driver_bus_adc_read,
    .ioctl = driver_bus_adc_ioctl,
    .shutdown = driver_bus_adc_shutdown,
};

#endif
