#include "includes.h"
#ifdef EOS_DRV_POWER_BATTERY_ENABLED

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"

// ── LiPo Discharge Chemistry Curve ────────────────────────────

typedef struct {
  uint16_t mv;
  uint8_t soc;
} lipo_lut_t;

// 12-point empirical 1S LiPo Open-Circuit Voltage (OCV) profile
static const lipo_lut_t k_lipo_curve[] = {
    {4180, 100},
    {4100,  90},
    {4000,  80},
    {3900,  70},
    {3830,  60},
    {3780,  50},
    {3740,  40},
    {3700,  30},
    {3650,  20},
    {3580,  10},
    {3450,   5},
    {3200,   0},
};

#define K_LIPO_CURVE_POINTS (sizeof(k_lipo_curve) / sizeof(k_lipo_curve[0]))

// ── State ─────────────────────────────────────────────────────

typedef struct {
  int32_t adc_pin;       // GPIO pin connected to voltage divider (default 3)
  uint32_t r_top;        // Top resistor in kΩ (R4 = 33 kΩ)
  uint32_t r_bottom;     // Bottom resistor in kΩ (R2 = 100 kΩ)
  uint32_t v_empty_mv;   // Empty cutoff threshold in mV (default 3200)
  uint32_t v_full_mv;    // Full threshold in mV (default 4200)

  // Filter & Smoothing State
  uint32_t smoothed_mv;  // Exponential moving average of battery voltage
  int32_t last_soc;      // Last reported SoC (for hysteresis)
  int32_t prev_trend_mv; // Voltage recorded at previous interval to check dV/dt
  eos_battery_status_t last_status;
  int32_t read_offset;

  SemaphoreHandle_t lock;
} battery_state_t;

// ── Non-linear SoC Calculation ────────────────────────────────

static int32_t battery_calculate_soc(uint32_t v_cell_mv, uint32_t v_empty,
                                     uint32_t v_full) {
  if (v_cell_mv >= v_full || v_cell_mv >= k_lipo_curve[0].mv) {
    return 100;
  }
  if (v_cell_mv <= v_empty ||
      v_cell_mv <= k_lipo_curve[K_LIPO_CURVE_POINTS - 1].mv) {
    return 0;
  }

  // Piecewise linear interpolation between curve points
  for (size_t i = 0; i < K_LIPO_CURVE_POINTS - 1; i++) {
    uint16_t v_high = k_lipo_curve[i].mv;
    uint16_t v_low = k_lipo_curve[i + 1].mv;
    uint8_t soc_high = k_lipo_curve[i].soc;
    uint8_t soc_low = k_lipo_curve[i + 1].soc;

    if (v_cell_mv <= v_high && v_cell_mv >= v_low) {
      uint32_t span_v = v_high - v_low;
      if (span_v == 0)
        return soc_low;
      uint32_t delta_v = v_cell_mv - v_low;
      uint32_t span_soc = soc_high - soc_low;
      int32_t soc = (int32_t)(soc_low + (delta_v * span_soc) / span_v);
      if (soc > 100)
        soc = 100;
      if (soc < 0)
        soc = 0;
      return soc;
    }
  }

  return 0;
}

// ── Sample & Median Filter ────────────────────────────────────

static int compare_int32(const void *a, const void *b) {
  int32_t va = *(const int32_t *)a;
  int32_t vb = *(const int32_t *)b;
  return (va > vb) - (va < vb);
}

static bool battery_sample_hardware(eos_dev_t *dev, battery_state_t *st,
                                    uint32_t *out_vbat_mv, bool *out_present) {
  if (!dev || !st)
    return false;

  eos_dev_t *adc_dev = dev->parent;
  if (!adc_dev) {
    return false;
  }

  // Sample ADC 16 times to compute median
  int32_t pin_samples[16];
  size_t valid_count = 0;

  for (size_t i = 0; i < 16; i++) {
    eos_adc_reading_t reading = {
        .pin = (uint32_t)st->adc_pin,
        .raw = 0,
        .voltage_mv = 0,
    };

    if (eos_dev_ioctl_call(adc_dev, EOS_DRV_FD, EOS_ADC_IOCTL_READ_VOLTAGE,
                           &reading) == 0) {
      pin_samples[valid_count++] = reading.voltage_mv;
    }
  }

  if (valid_count == 0) {
    return false;
  }

  // Sort and select median
  qsort(pin_samples, valid_count, sizeof(int32_t), compare_int32);
  int32_t median_pin_mv = pin_samples[valid_count / 2];

  // If pin voltage is < 500 mV, battery is not connected (pulled down by circuit)
  if (median_pin_mv < 500) {
    if (out_present)
      *out_present = false;
    if (out_vbat_mv)
      *out_vbat_mv = 0;
    return true;
  }

  if (out_present)
    *out_present = true;

  // Scale via resistor ladder: Vbat = Vpin * (R_top + R_bottom) / R_bottom
  uint64_t vbat_calc =
      ((uint64_t)median_pin_mv * (st->r_top + st->r_bottom)) / st->r_bottom;
  uint32_t vbat_mv = (uint32_t)vbat_calc;

  if (out_vbat_mv) {
    *out_vbat_mv = vbat_mv;
  }

  return true;
}

// ── Update State and SoC with Smoothing ───────────────────────

static bool battery_update_info(eos_dev_t *dev, battery_state_t *st,
                                eos_battery_info_t *info) {
  if (!dev || !st || !info)
    return false;

  uint32_t raw_vbat_mv = 0;
  bool present = false;

  if (!battery_sample_hardware(dev, st, &raw_vbat_mv, &present)) {
    return false;
  }

  if (!present) {
    info->voltage_mv = 0;
    info->soc_percent = -1;
    info->status = EOS_BATTERY_STATUS_NOT_PRESENT;
    info->v_empty_mv = st->v_empty_mv;
    info->v_full_mv = st->v_full_mv;
    st->smoothed_mv = 0;
    st->last_soc = -1;
    st->last_status = EOS_BATTERY_STATUS_NOT_PRESENT;
    return true;
  }

  // EMA smoothing: smoothed = (alpha * raw + (1-alpha) * prev)
  // Alpha = 0.2 -> (raw * 2 + prev * 8) / 10
  if (st->smoothed_mv == 0) {
    st->smoothed_mv = raw_vbat_mv;
    st->prev_trend_mv = raw_vbat_mv;
  } else {
    st->smoothed_mv = (raw_vbat_mv * 2 + st->smoothed_mv * 8) / 10;
  }

  uint32_t v_cell = st->smoothed_mv;
  int32_t instant_soc =
      battery_calculate_soc(v_cell, st->v_empty_mv, st->v_full_mv);

  // Status and Trend Detection
  eos_battery_status_t status = EOS_BATTERY_STATUS_DISCHARGING;

  if (v_cell >= 4180) {
    status = EOS_BATTERY_STATUS_FULL;
  } else if ((int32_t)v_cell - st->prev_trend_mv > 30) {
    // Voltage is climbing by >30mV over measurement window -> Charging
    status = EOS_BATTERY_STATUS_CHARGING;
  } else {
    status = EOS_BATTERY_STATUS_DISCHARGING;
  }

  // Update trend tracking mark slowly
  st->prev_trend_mv = (st->prev_trend_mv * 9 + (int32_t)v_cell) / 10;

  // Anti-Flicker Monotonicity during discharge:
  // When discharging, do not let SoC fluctuate upward by 1-2% due to momentary CPU idle
  int32_t final_soc = instant_soc;
  if (status == EOS_BATTERY_STATUS_DISCHARGING && st->last_soc >= 0) {
    if (instant_soc > st->last_soc && (instant_soc - st->last_soc) < 3) {
      final_soc = st->last_soc;
    }
  }

  st->last_soc = final_soc;
  st->last_status = status;

  info->voltage_mv = v_cell;
  info->soc_percent = final_soc;
  info->status = status;
  info->v_empty_mv = st->v_empty_mv;
  info->v_full_mv = st->v_full_mv;

  return true;
}

// ── Lifecycle ─────────────────────────────────────────────────

static bool driver_power_battery_init(eos_dev_t *dev) {
  if (!dev)
    return false;

  battery_state_t *st = calloc(1, sizeof(battery_state_t));
  if (!st)
    return false;

  st->lock = xSemaphoreCreateMutex();
  if (!st->lock) {
    free(st);
    return false;
  }

  // Resolve ADC pin (Lilka default: GPIO 3)
  st->adc_pin = eos_pin_get_no(dev->pins, "adc");
  if (st->adc_pin < 0)
    st->adc_pin = eos_pin_get_no(dev->pins, "vbat");
  if (st->adc_pin < 0)
    st->adc_pin = (int32_t)eos_cfg_get_i(dev->cfg, "pin", 3);

  // Resistor ladder: Lilka v2.3 has R4=33k (top), R2=100k (bottom)
  st->r_top = (uint32_t)eos_cfg_get_i(dev->cfg, "r_top", 33);
  if (st->r_top == 0)
    st->r_top = 33;

  st->r_bottom = (uint32_t)eos_cfg_get_i(dev->cfg, "r_bottom", 100);
  if (st->r_bottom == 0)
    st->r_bottom = 100;

  // Thresholds
  st->v_empty_mv = (uint32_t)eos_cfg_get_i(dev->cfg, "v_empty", 3200);
  st->v_full_mv = (uint32_t)eos_cfg_get_i(dev->cfg, "v_full", 4200);

  st->last_soc = -1;
  st->last_status = EOS_BATTERY_STATUS_NOT_PRESENT;

  dev->state = st;
  EOS_LOGI("battery: ready (pin=%" PRId32 ", divider=%" PRIu32 "k/%" PRIu32
           "k, empty=%" PRIu32 "mV, full=%" PRIu32 "mV)",
           st->adc_pin, st->r_top, st->r_bottom, st->v_empty_mv, st->v_full_mv);
  return true;
}

static void driver_power_battery_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state)
    return;

  battery_state_t *st = dev->state;

  if (st->lock) {
    xSemaphoreTake(st->lock, portMAX_DELAY);
    xSemaphoreGive(st->lock);
    vSemaphoreDelete(st->lock);
    st->lock = NULL;
  }

  free(st);
  dev->state = NULL;
}

// ── VFS Operations ────────────────────────────────────────────

static const char *status_to_string(eos_battery_status_t status) {
  switch (status) {
  case EOS_BATTERY_STATUS_CHARGING:
    return "charging";
  case EOS_BATTERY_STATUS_FULL:
    return "full";
  case EOS_BATTERY_STATUS_DISCHARGING:
    return "discharging";
  case EOS_BATTERY_STATUS_NOT_PRESENT:
  default:
    return "not_present";
  }
}

static int driver_power_battery_open(eos_dev_t *dev, const char *path,
                                    int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state)
    return -1;
  battery_state_t *st = dev->state;
  st->read_offset = 0;
  return 0;
}

static int driver_power_battery_close(eos_dev_t *dev, int fd) {
  (void)fd;
  if (!dev || !dev->state)
    return -1;
  battery_state_t *st = dev->state;
  st->read_offset = 0;
  return 0;
}

static ssize_t driver_power_battery_read(eos_dev_t *dev, int fd, void *dst,
                                        size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst || size == 0) {
    errno = EINVAL;
    return -1;
  }

  battery_state_t *st = dev->state;
  if (xSemaphoreTake(st->lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  if (st->read_offset > 0) {
    xSemaphoreGive(st->lock);
    return 0; // EOF for cat /dev/battery0
  }

  eos_battery_info_t info = {0};
  bool ok = battery_update_info(dev, st, &info);

  if (!ok) {
    xSemaphoreGive(st->lock);
    errno = EIO;
    return -1;
  }

  char buf[128];
  int n = 0;
  if (info.status == EOS_BATTERY_STATUS_NOT_PRESENT) {
    n = snprintf(buf, sizeof(buf),
                 "present: 0\nstatus: not_present\npercentage: -1%%\nvoltage: 0 mV\n");
  } else {
    n = snprintf(buf, sizeof(buf),
                 "present: 1\nstatus: %s\npercentage: %" PRId32 "%%\nvoltage: %" PRIu32
                 " mV\nv_empty: %" PRIu32 " mV\nv_full: %" PRIu32 " mV\n",
                 status_to_string(info.status), info.soc_percent,
                 info.voltage_mv, info.v_empty_mv, info.v_full_mv);
  }

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

static int driver_power_battery_ioctl(eos_dev_t *dev, int fd, int cmd,
                                      va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }

  battery_state_t *st = dev->state;
  if (xSemaphoreTake(st->lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  int ret = 0;
  switch (cmd) {
  case EOS_POWER_IOCTL_GET_BATTERY_INFO: {
    eos_battery_info_t *out = va_arg(args, eos_battery_info_t *);
    if (!out) {
      errno = EINVAL;
      ret = -1;
    } else {
      if (!battery_update_info(dev, st, out)) {
        errno = EIO;
        ret = -1;
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

EOS_DRV_ATTR eos_drv_t driver_power_battery = {
    EOS_DRV_INIT,
    .scope = "power",
    .name = "battery",
    .devname = "battery",
    .init = driver_power_battery_init,
    .open = driver_power_battery_open,
    .close = driver_power_battery_close,
    .read = driver_power_battery_read,
    .ioctl = driver_power_battery_ioctl,
    .shutdown = driver_power_battery_shutdown,
};

#endif
