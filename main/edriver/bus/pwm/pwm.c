#include "includes.h"
#ifdef EOS_DRV_BUS_PWM_ENABLED

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"
#include "emisc/strlimits.h"
#include "pwm.h"

static int32_t pwm_find_matching_timer(eos_dev_t *dev, uint32_t freq_hz) {
  size_t cnt = eos_dev_count();
  for (size_t i = 0; i < cnt; i++) {
    eos_dev_t *d = eos_dev_get(i);
    if (!d || d == dev || !d->driver || !d->state) {
      continue;
    }
    if (strcmp(d->driver->name, "pwm") == 0) {
      pwm_dev_state_t *other = (pwm_dev_state_t *)d->state;
      if (other->freq_hz == freq_hz && other->timer_idx >= 0) {
        return other->timer_idx;
      }
    }
  }
  return -1;
}

static bool pwm_is_timer_conflicted(eos_dev_t *dev, int32_t timer_idx, uint32_t freq_hz) {
  size_t cnt = eos_dev_count();
  for (size_t i = 0; i < cnt; i++) {
    eos_dev_t *d = eos_dev_get(i);
    if (!d || d == dev || !d->driver || !d->state) {
      continue;
    }
    if (strcmp(d->driver->name, "pwm") == 0) {
      pwm_dev_state_t *other = (pwm_dev_state_t *)d->state;
      if (other->timer_idx == timer_idx && other->freq_hz != freq_hz) {
        return true;
      }
    }
  }
  return false;
}

static int32_t pwm_allocate_timer(eos_dev_t *dev, uint32_t freq_hz, int32_t req_timer, bool *out_owner) {
  if (req_timer >= 0) {
    if (pwm_is_timer_conflicted(dev, req_timer, freq_hz)) {
      EOS_LOGE("pwm: timer %" PRId32 " already configured at different frequency", req_timer);
      return -1;
    }
    int32_t shared = pwm_find_matching_timer(dev, freq_hz);
    if (shared == req_timer) {
      if (out_owner) *out_owner = false;
      return req_timer;
    }
    if (!eos_cap_claim(EOS_CAPS_LEDC_TIMER, req_timer, dev)) {
      EOS_LOGE("pwm: timer %" PRId32 " claim failed", req_timer);
      return -1;
    }
    if (out_owner) *out_owner = true;
    return req_timer;
  }

  int32_t shared_timer = pwm_find_matching_timer(dev, freq_hz);
  if (shared_timer >= 0) {
    if (out_owner) *out_owner = false;
    return shared_timer;
  }

  int32_t max_timers = eos_cap_get_max_id(EOS_CAPS_LEDC_TIMER);
  for (int32_t i = 0; i < max_timers; i++) {
    if (eos_cap_claim(EOS_CAPS_LEDC_TIMER, i, dev)) {
      if (out_owner) *out_owner = true;
      return i;
    }
  }

  EOS_LOGE("pwm: no free LEDC timer available");
  return -1;
}

static int32_t pwm_allocate_channel(eos_dev_t *dev, int32_t req_channel) {
  if (req_channel >= 0) {
    if (!eos_cap_claim(EOS_CAPS_LEDC_CHANNEL, req_channel, dev)) {
      EOS_LOGE("pwm: channel %" PRId32 " claim failed", req_channel);
      return -1;
    }
    return req_channel;
  }

  int32_t max_channels = eos_cap_get_max_id(EOS_CAPS_LEDC_CHANNEL);
  for (int32_t i = 0; i < max_channels; i++) {
    if (eos_cap_claim(EOS_CAPS_LEDC_CHANNEL, i, dev)) {
      return i;
    }
  }

  EOS_LOGE("pwm: no free LEDC channel available");
  return -1;
}

static uint32_t pwm_duty16_to_hw(pwm_dev_state_t *st, uint16_t duty16) {
  if (!st || st->hw_duty_max == 0) return 0;
  return (uint32_t)(((uint64_t)duty16 * (uint64_t)st->hw_duty_max) / (uint64_t)EOS_PWM_DUTY_MAX_16BIT);
}

static int pwm_apply_duty(pwm_dev_state_t *st, uint16_t duty16) {
  if (!st || st->channel_idx < 0) return -EINVAL;
  st->duty16 = duty16;
  st->duty_pct = (uint8_t)(((uint32_t)duty16 * (uint32_t)EOS_PWM_PERCENT_MAX) / (uint32_t)EOS_PWM_DUTY_MAX_16BIT);

  uint32_t hw_val = st->enabled ? pwm_duty16_to_hw(st, duty16) : 0;
  esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)st->channel_idx, hw_val);
  if (err != ESP_OK) return -EIO;
  err = ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)st->channel_idx);
  return (err == ESP_OK) ? 0 : -EIO;
}

static int pwm_apply_fade(pwm_dev_state_t *st, uint16_t target_duty, uint32_t duration_ms) {
  if (!st || st->channel_idx < 0) return -EINVAL;
  st->duty16 = target_duty;
  st->duty_pct = (uint8_t)(((uint32_t)target_duty * (uint32_t)EOS_PWM_PERCENT_MAX) / (uint32_t)EOS_PWM_DUTY_MAX_16BIT);

  uint32_t target_hw = st->enabled ? pwm_duty16_to_hw(st, target_duty) : 0;
  if (duration_ms == 0) {
    return pwm_apply_duty(st, target_duty);
  }

  esp_err_t err = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, (ledc_channel_t)st->channel_idx, target_hw, duration_ms);
  if (err != ESP_OK) {
    return pwm_apply_duty(st, target_duty);
  }
  err = ledc_fade_start(LEDC_LOW_SPEED_MODE, (ledc_channel_t)st->channel_idx, LEDC_FADE_NO_WAIT);
  return (err == ESP_OK) ? 0 : -EIO;
}

static void pwm_release_shared_timer_if_unused(eos_dev_t *dev, int32_t timer_idx) {
  if (timer_idx < 0) return;
  size_t cnt = eos_dev_count();
  bool in_use = false;
  for (size_t i = 0; i < cnt; i++) {
    eos_dev_t *d = eos_dev_get(i);
    if (!d || d == dev || !d->driver || !d->state) {
      continue;
    }
    if (strcmp(d->driver->name, "pwm") == 0) {
      pwm_dev_state_t *other = (pwm_dev_state_t *)d->state;
      if (other->timer_idx == timer_idx) {
        in_use = true;
        break;
      }
    }
  }
  if (!in_use) {
    eos_cap_release(EOS_CAPS_LEDC_TIMER, timer_idx, dev);
  }
}

bool driver_bus_pwm_init(eos_dev_t *dev) {
  if (!dev) return false;

  int32_t pin = eos_pin_get_no(dev->pins, "out");
  if (pin < 0) pin = eos_pin_get_no(dev->pins, "pwm");
  if (pin < 0) pin = eos_pin_get_no(dev->pins, "pin");
  if (pin < 0) {
    EOS_LOGE("pwm: output pin not specified (use --pin out=<num>)");
    return false;
  }

  uint32_t freq = (uint32_t)eos_cfg_get_i(dev->cfg, "freq", EOS_PWM_DEFAULT_FREQ_HZ);
  int32_t initial_duty_pct = eos_cfg_get_i(dev->cfg, "duty", EOS_PWM_DEFAULT_DUTY_PCT);
  int32_t req_timer = eos_cfg_get_i(dev->cfg, "timer", -1);
  int32_t req_chan = eos_cfg_get_i(dev->cfg, "channel", -1);
  uint32_t polarity = (uint32_t)eos_cfg_get_i(dev->cfg, "polarity", 0);

  if (!eos_cap_claim(EOS_CAPS_GPIO, pin, dev)) {
    EOS_LOGE("pwm: GPIO %" PRId32 " already in use", pin);
    return false;
  }

  bool timer_owner = false;
  int32_t timer_idx = pwm_allocate_timer(dev, freq, req_timer, &timer_owner);
  if (timer_idx < 0) {
    eos_cap_release(EOS_CAPS_GPIO, pin, dev);
    return false;
  }

  int32_t channel_idx = pwm_allocate_channel(dev, req_chan);
  if (channel_idx < 0) {
    if (timer_owner) eos_cap_release(EOS_CAPS_LEDC_TIMER, timer_idx, dev);
    eos_cap_release(EOS_CAPS_GPIO, pin, dev);
    return false;
  }

  pwm_dev_state_t *st = (pwm_dev_state_t *)calloc(1, sizeof(pwm_dev_state_t));
  if (!st) {
    eos_cap_release(EOS_CAPS_LEDC_CHANNEL, channel_idx, dev);
    if (timer_owner) eos_cap_release(EOS_CAPS_LEDC_TIMER, timer_idx, dev);
    eos_cap_release(EOS_CAPS_GPIO, pin, dev);
    return false;
  }

  st->pin = pin;
  st->freq_hz = freq;
  st->timer_idx = timer_idx;
  st->channel_idx = channel_idx;
  st->timer_owner = timer_owner;
  st->polarity = polarity;
  st->enabled = true;
  st->resolution = (ledc_timer_bit_t)EOS_PWM_RES_BITS_DEFAULT;
  st->hw_duty_max = EOS_PWM_HW_DUTY_MAX_DEFAULT;

  if (initial_duty_pct < 0) initial_duty_pct = 0;
  if (initial_duty_pct > EOS_PWM_PERCENT_MAX) initial_duty_pct = EOS_PWM_PERCENT_MAX;
  st->duty_pct = (uint8_t)initial_duty_pct;
  st->duty16 = (uint16_t)(((uint32_t)st->duty_pct * (uint32_t)EOS_PWM_DUTY_MAX_16BIT) / (uint32_t)EOS_PWM_PERCENT_MAX);

  ledc_timer_config_t timer_conf = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = st->resolution,
      .timer_num = (ledc_timer_t)st->timer_idx,
      .freq_hz = st->freq_hz,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  esp_err_t err = ledc_timer_config(&timer_conf);
  if (err != ESP_OK) {
    EOS_LOGE("pwm: ledc_timer_config failed: %s", esp_err_to_name(err));
    free(st);
    eos_cap_release(EOS_CAPS_LEDC_CHANNEL, channel_idx, dev);
    if (timer_owner) eos_cap_release(EOS_CAPS_LEDC_TIMER, timer_idx, dev);
    eos_cap_release(EOS_CAPS_GPIO, pin, dev);
    return false;
  }

  uint32_t hw_val = pwm_duty16_to_hw(st, st->duty16);
  ledc_channel_config_t ch_conf = {
      .gpio_num = st->pin,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = (ledc_channel_t)st->channel_idx,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = (ledc_timer_t)st->timer_idx,
      .duty = hw_val,
      .hpoint = 0,
      .flags.output_invert = (st->polarity != 0) ? 1 : 0,
  };
  err = ledc_channel_config(&ch_conf);
  if (err != ESP_OK) {
    EOS_LOGE("pwm: ledc_channel_config failed: %s", esp_err_to_name(err));
    free(st);
    eos_cap_release(EOS_CAPS_LEDC_CHANNEL, channel_idx, dev);
    if (timer_owner) eos_cap_release(EOS_CAPS_LEDC_TIMER, timer_idx, dev);
    eos_cap_release(EOS_CAPS_GPIO, pin, dev);
    return false;
  }

  ledc_fade_func_install(0);
  dev->state = st;
  EOS_LOGI("pwm: attached on GPIO %" PRId32 " (timer=%" PRId32 ", chan=%" PRId32 ", freq=%" PRIu32 "Hz, duty=%u%%)",
           st->pin, st->timer_idx, st->channel_idx, st->freq_hz, (unsigned)st->duty_pct);
  return true;
}

void driver_bus_pwm_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  pwm_dev_state_t *st = (pwm_dev_state_t *)dev->state;

  ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)st->channel_idx, 0);
  gpio_set_direction(st->pin, GPIO_MODE_INPUT);

  eos_cap_release(EOS_CAPS_LEDC_CHANNEL, st->channel_idx, dev);
  pwm_release_shared_timer_if_unused(dev, st->timer_idx);
  eos_cap_release(EOS_CAPS_GPIO, st->pin, dev);

  free(st);
  dev->state = NULL;
}

int driver_bus_pwm_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) return -ENODEV;
  pwm_dev_state_t *st = (pwm_dev_state_t *)dev->state;
  st->read_offset = 0;
  return 0;
}

int driver_bus_pwm_close(eos_dev_t *dev, int fd) {
  (void)fd;
  if (!dev || !dev->state) return -ENODEV;
  pwm_dev_state_t *st = (pwm_dev_state_t *)dev->state;
  st->read_offset = 0;
  return 0;
}

int driver_bus_pwm_fstat(eos_dev_t *dev, int fd, struct stat *st) {
  (void)dev;
  (void)fd;
  if (!st) return -EINVAL;
  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFCHR | EOS_DEFAULT_DEV_MODE;
  return 0;
}

ssize_t driver_bus_pwm_read(eos_dev_t *dev, int fd, void *buf, size_t count) {
  (void)fd;
  if (!dev || !dev->state || !buf || count == 0) return 0;
  pwm_dev_state_t *st = (pwm_dev_state_t *)dev->state;

  if (st->read_offset > 0) {
    return 0; // EOF for cat /dev/pwm0
  }

  char tmp[EOS_PWM_READ_BUF_LEN];
  snprintf(tmp, sizeof(tmp), "%u\n", (unsigned)st->duty_pct);
  size_t len = strlen(tmp);

  size_t to_copy = (count < len) ? count : len;
  memcpy(buf, tmp, to_copy);
  st->read_offset += (int32_t)to_copy;
  return (ssize_t)to_copy;
}

ssize_t driver_bus_pwm_write(eos_dev_t *dev, int fd, const void *buf, size_t count) {
  (void)fd;
  if (!dev || !dev->state || !buf || count == 0) return 0;
  pwm_dev_state_t *st = (pwm_dev_state_t *)dev->state;

  char tmp[EOS_PWM_READ_BUF_LEN];
  size_t len = (count < sizeof(tmp) - 1) ? count : (sizeof(tmp) - 1);
  memcpy(tmp, buf, len);
  tmp[len] = '\0';

  char *endptr = NULL;
  long val = strtol(tmp, &endptr, EOS_PWM_DECIMAL_BASE);
  if (endptr == tmp) {
    return -EINVAL;
  }
  if (val < 0) val = 0;
  if (val > EOS_PWM_PERCENT_MAX) val = EOS_PWM_PERCENT_MAX;

  uint16_t duty16 = (uint16_t)(((uint32_t)val * (uint32_t)EOS_PWM_DUTY_MAX_16BIT) / (uint32_t)EOS_PWM_PERCENT_MAX);
  if (pwm_apply_duty(st, duty16) != 0) {
    return -EIO;
  }
  return (ssize_t)count;
}

int driver_bus_pwm_ioctl(eos_dev_t *dev, int fd, int request, va_list args) {
  (void)fd;
  if (!dev || !dev->state) return -EINVAL;
  pwm_dev_state_t *st = (pwm_dev_state_t *)dev->state;

  switch (request) {
  case EOS_PWM_IOCTL_SET_DUTY: {
    uint32_t duty_arg = va_arg(args, uint32_t);
    uint16_t duty16 = (duty_arg <= EOS_PWM_PERCENT_MAX)
                          ? (uint16_t)(((uint32_t)duty_arg * (uint32_t)EOS_PWM_DUTY_MAX_16BIT) / (uint32_t)EOS_PWM_PERCENT_MAX)
                          : (uint16_t)duty_arg;
    return pwm_apply_duty(st, duty16);
  }

  case EOS_PWM_IOCTL_GET_DUTY: {
    uint16_t *out = va_arg(args, uint16_t *);
    if (!out) return -EINVAL;
    *out = st->duty16;
    return 0;
  }

  case EOS_PWM_IOCTL_SET_FREQ: {
    uint32_t new_freq = va_arg(args, uint32_t);
    if (new_freq == 0) return -EINVAL;
    if (pwm_is_timer_conflicted(dev, st->timer_idx, new_freq)) {
      return -EBUSY;
    }
    esp_err_t err = ledc_set_freq(LEDC_LOW_SPEED_MODE, (ledc_timer_t)st->timer_idx, new_freq);
    if (err != ESP_OK) return -EIO;
    st->freq_hz = new_freq;
    return 0;
  }

  case EOS_PWM_IOCTL_GET_FREQ: {
    uint32_t *out = va_arg(args, uint32_t *);
    if (!out) return -EINVAL;
    *out = st->freq_hz;
    return 0;
  }

  case EOS_PWM_IOCTL_ENABLE: {
    bool enable = (bool)va_arg(args, int);
    st->enabled = enable;
    return pwm_apply_duty(st, st->duty16);
  }

  case EOS_PWM_IOCTL_SET_POLARITY: {
    uint32_t pol = va_arg(args, uint32_t);
    st->polarity = pol ? 1 : 0;
    return 0;
  }

  case EOS_PWM_IOCTL_FADE: {
    const eos_pwm_fade_t *fade = va_arg(args, const eos_pwm_fade_t *);
    if (!fade) return -EINVAL;
    return pwm_apply_fade(st, fade->target_duty, fade->duration_ms);
  }

  case EOS_PWM_IOCTL_GET_CAPS: {
    eos_pwm_caps_t *caps = va_arg(args, eos_pwm_caps_t *);
    if (!caps) return -EINVAL;
    caps->freq_hz = st->freq_hz;
    caps->resolution_bits = (uint32_t)st->resolution;
    caps->max_duty = st->hw_duty_max;
    caps->timer_idx = st->timer_idx;
    caps->channel_idx = st->channel_idx;
    caps->pin = st->pin;
    caps->enabled = st->enabled;
    return 0;
  }

  default:
    return -ENOTTY;
  }
}

EOS_DRV_ATTR eos_drv_t driver_bus_pwm = {
    EOS_DRV_INIT,
    .scope    = "bus",
    .name     = "pwm",
    .devname  = "pwm",
    .init     = driver_bus_pwm_init,
    .shutdown = driver_bus_pwm_shutdown,
    .open     = driver_bus_pwm_open,
    .close    = driver_bus_pwm_close,
    .fstat    = driver_bus_pwm_fstat,
    .read     = driver_bus_pwm_read,
    .write    = driver_bus_pwm_write,
    .ioctl    = driver_bus_pwm_ioctl,
};

#endif
