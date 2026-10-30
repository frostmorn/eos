#include "includes.h"
#ifdef EOS_DRV_SOUND_MAX98357_ENABLED

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ecore/capsmgr.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/types.h"

// ── State ─────────────────────────────────────────────────────

typedef struct {
  i2s_chan_handle_t tx_chan;
  int32_t bclk_pin;
  int32_t ws_pin;
  int32_t dout_pin;
  uint32_t sample_rate;
  uint32_t bits_per_sample;
  uint32_t channels;
  uint32_t volume; // User volume 0 - 100
  uint32_t mult;   // Power multiplier %, default 10 (earplugs mode, 10% max)
  bool enabled;
  SemaphoreHandle_t lock;
} max98357_state_t;

// ── Reconfiguration Helper ────────────────────────────────────

static bool max98357_apply_format(max98357_state_t *st, uint32_t rate,
                                  uint32_t bits, uint32_t channels) {
  if (!st || !st->tx_chan)
    return false;

  if (rate == 0 || (bits != 16 && bits != 24 && bits != 32) ||
      (channels != 1 && channels != 2)) {
    return false;
  }

  bool was_enabled = st->enabled;
  if (was_enabled) {
    i2s_channel_disable(st->tx_chan);
  }

  i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  i2s_data_bit_width_t data_bit_width =
      (bits == 32)   ? I2S_DATA_BIT_WIDTH_32BIT
      : (bits == 24) ? I2S_DATA_BIT_WIDTH_24BIT
                     : I2S_DATA_BIT_WIDTH_16BIT;
  i2s_slot_mode_t slot_mode =
      (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
  i2s_std_slot_config_t slot_cfg =
      I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(data_bit_width, slot_mode);
  slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

  esp_err_t err = i2s_channel_reconfig_std_clock(st->tx_chan, &clk_cfg);
  if (err != ESP_OK) {
    EOS_LOGE("max98357: failed to reconfig clock: %s", esp_err_to_name(err));
    if (was_enabled)
      i2s_channel_enable(st->tx_chan);
    return false;
  }

  err = i2s_channel_reconfig_std_slot(st->tx_chan, &slot_cfg);
  if (err != ESP_OK) {
    EOS_LOGE("max98357: failed to reconfig slot: %s", esp_err_to_name(err));
    if (was_enabled)
      i2s_channel_enable(st->tx_chan);
    return false;
  }

  st->sample_rate = rate;
  st->bits_per_sample = bits;
  st->channels = channels;

  if (was_enabled) {
    i2s_channel_enable(st->tx_chan);
  }

  return true;
}

// ── Init / Shutdown ───────────────────────────────────────────

static bool driver_sound_max98357_init(eos_dev_t *dev) {
  if (!dev)
    return false;

  max98357_state_t *st = calloc(1, sizeof(max98357_state_t));
  if (!st)
    return false;
  dev->state = st;

  st->lock = xSemaphoreCreateMutex();
  if (!st->lock) {
    free(st);
    dev->state = NULL;
    return false;
  }

  // Pin resolution with fallback to Lilka v2.3 defaults (bclk=42, ws=1, dout=2)
  st->bclk_pin = eos_pin_get_no(dev->pins, "bclk");
  if (st->bclk_pin < 0)
    st->bclk_pin = eos_pin_get_no(dev->pins, "sck");
  if (st->bclk_pin < 0)
    st->bclk_pin = 42;

  st->ws_pin = eos_pin_get_no(dev->pins, "ws");
  if (st->ws_pin < 0)
    st->ws_pin = eos_pin_get_no(dev->pins, "lrck");
  if (st->ws_pin < 0)
    st->ws_pin = 1;

  st->dout_pin = eos_pin_get_no(dev->pins, "dout");
  if (st->dout_pin < 0)
    st->dout_pin = eos_pin_get_no(dev->pins, "data");
  if (st->dout_pin < 0)
    st->dout_pin = eos_pin_get_no(dev->pins, "din");
  if (st->dout_pin < 0)
    st->dout_pin = 2;

  // Claim GPIO capabilities
  if (!eos_cap_claim(EOS_CAPS_GPIO, st->bclk_pin, dev)) {
    EOS_LOGE("max98357: failed to claim bclk pin %" PRId32, st->bclk_pin);
    vSemaphoreDelete(st->lock);
    free(st);
    dev->state = NULL;
    return false;
  }
  if (!eos_cap_claim(EOS_CAPS_GPIO, st->ws_pin, dev)) {
    EOS_LOGE("max98357: failed to claim ws pin %" PRId32, st->ws_pin);
    eos_cap_release(EOS_CAPS_GPIO, st->bclk_pin, dev);
    vSemaphoreDelete(st->lock);
    free(st);
    dev->state = NULL;
    return false;
  }
  if (!eos_cap_claim(EOS_CAPS_GPIO, st->dout_pin, dev)) {
    EOS_LOGE("max98357: failed to claim dout pin %" PRId32, st->dout_pin);
    eos_cap_release(EOS_CAPS_GPIO, st->ws_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->bclk_pin, dev);
    vSemaphoreDelete(st->lock);
    free(st);
    dev->state = NULL;
    return false;
  }

  // Parse configurations
  st->sample_rate = (uint32_t)eos_cfg_get_i(dev->cfg, "rate", 44100);
  if (st->sample_rate == 0)
    st->sample_rate = 44100;

  st->bits_per_sample = (uint32_t)eos_cfg_get_i(dev->cfg, "bits", 16);
  if (st->bits_per_sample != 16 && st->bits_per_sample != 24 &&
      st->bits_per_sample != 32) {
    st->bits_per_sample = 16;
  }

  st->channels = (uint32_t)eos_cfg_get_i(dev->cfg, "channels", 1);
  if (st->channels != 1 && st->channels != 2)
    st->channels = 1;

  st->volume = (uint32_t)eos_cfg_get_i(dev->cfg, "vol", 100);
  if (st->volume > 100)
    st->volume = 100;

  // Multiplier setting: default 10% (earplugs mode, protecting ears from 3W amp)
  uint32_t mult = 10;
  int32_t cfg_mult = eos_cfg_get_i(dev->cfg, "mult", -1);
  if (cfg_mult < 0)
    cfg_mult = eos_cfg_get_i(dev->cfg, "multiplier", -1);

  if (cfg_mult >= 0) {
    mult = (uint32_t)cfg_mult;
  } else {
    int32_t ep = eos_cfg_get_i(dev->cfg, "earplugs", -1);
    if (ep == 0) {
      mult = 100; // full power if earplugs mode explicitly disabled
    } else {
      char *mode = eos_cfg_get_s(dev->cfg, "mode", NULL);
      if (mode && (strcasecmp(mode, "speaker") == 0 ||
                   strcasecmp(mode, "full") == 0 ||
                   strcasecmp(mode, "loud") == 0)) {
        mult = 100;
      } else {
        mult = 10; // Default earplugs mode: 10% multiplier
      }
    }
  }
  if (mult > 100)
    mult = 100;
  st->mult = mult;

  // Initialize I2S TX channel
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 6;
  chan_cfg.dma_frame_num = 240;

  esp_err_t err = i2s_new_channel(&chan_cfg, &st->tx_chan, NULL);
  if (err != ESP_OK) {
    EOS_LOGE("max98357: failed to create i2s channel: %s", esp_err_to_name(err));
    eos_cap_release(EOS_CAPS_GPIO, st->dout_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->ws_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->bclk_pin, dev);
    vSemaphoreDelete(st->lock);
    free(st);
    dev->state = NULL;
    return false;
  }

  i2s_data_bit_width_t data_bit_width =
      (st->bits_per_sample == 32)   ? I2S_DATA_BIT_WIDTH_32BIT
      : (st->bits_per_sample == 24) ? I2S_DATA_BIT_WIDTH_24BIT
                                    : I2S_DATA_BIT_WIDTH_16BIT;
  i2s_slot_mode_t slot_mode =
      (st->channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(st->sample_rate),
      .slot_cfg =
          I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(data_bit_width, slot_mode),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)st->bclk_pin,
          .ws = (gpio_num_t)st->ws_pin,
          .dout = (gpio_num_t)st->dout_pin,
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

  err = i2s_channel_init_std_mode(st->tx_chan, &std_cfg);
  if (err != ESP_OK) {
    EOS_LOGE("max98357: failed to init std mode: %s", esp_err_to_name(err));
    i2s_del_channel(st->tx_chan);
    eos_cap_release(EOS_CAPS_GPIO, st->dout_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->ws_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->bclk_pin, dev);
    vSemaphoreDelete(st->lock);
    free(st);
    dev->state = NULL;
    return false;
  }

  err = i2s_channel_enable(st->tx_chan);
  if (err != ESP_OK) {
    EOS_LOGE("max98357: failed to enable i2s channel: %s", esp_err_to_name(err));
    i2s_del_channel(st->tx_chan);
    eos_cap_release(EOS_CAPS_GPIO, st->dout_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->ws_pin, dev);
    eos_cap_release(EOS_CAPS_GPIO, st->bclk_pin, dev);
    vSemaphoreDelete(st->lock);
    free(st);
    dev->state = NULL;
    return false;
  }

  st->enabled = true;
  EOS_LOGI("max98357: ready (bclk=%" PRId32 ", ws=%" PRId32 ", dout=%" PRId32
           ", rate=%" PRIu32 ", bits=%" PRIu32 ", ch=%" PRIu32 ", vol=%" PRIu32
           ", mult=%" PRIu32 "%%)",
           st->bclk_pin, st->ws_pin, st->dout_pin, st->sample_rate,
           st->bits_per_sample, st->channels, st->volume, st->mult);

  return true;
}

static void driver_sound_max98357_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state)
    return;

  max98357_state_t *st = dev->state;

  if (st->lock) {
    xSemaphoreTake(st->lock, portMAX_DELAY);
  }

  if (st->tx_chan) {
    if (st->enabled) {
      i2s_channel_disable(st->tx_chan);
      st->enabled = false;
    }
    i2s_del_channel(st->tx_chan);
    st->tx_chan = NULL;
  }

  if (st->dout_pin >= 0) {
    eos_cap_release(EOS_CAPS_GPIO, st->dout_pin, dev);
  }
  if (st->ws_pin >= 0) {
    eos_cap_release(EOS_CAPS_GPIO, st->ws_pin, dev);
  }
  if (st->bclk_pin >= 0) {
    eos_cap_release(EOS_CAPS_GPIO, st->bclk_pin, dev);
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

static ssize_t driver_sound_max98357_write(eos_dev_t *dev, int fd,
                                          const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data || size == 0) {
    errno = EINVAL;
    return -1;
  }

  max98357_state_t *st = dev->state;
  if (xSemaphoreTake(st->lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  if (!st->enabled || !st->tx_chan) {
    xSemaphoreGive(st->lock);
    errno = EIO;
    return -1;
  }

  size_t total_written = 0;
  esp_err_t err = ESP_OK;

  if (st->volume == 0 || st->mult == 0) {
    uint8_t silence[256] = {0};
    size_t remaining = size;
    while (remaining > 0) {
      size_t chunk = (remaining > sizeof(silence)) ? sizeof(silence) : remaining;
      size_t written = 0;
      err = i2s_channel_write(st->tx_chan, silence, chunk, &written,
                              pdMS_TO_TICKS(500));
      if (err != ESP_OK)
        break;
      total_written += written;
      remaining -= written;
      if (written == 0)
        break;
    }
  } else if (st->mult >= 100 && st->volume >= 100) {
    // 100% full scale output (full 3W power)
    err = i2s_channel_write(st->tx_chan, data, size, &total_written,
                            pdMS_TO_TICKS(2000));
  } else {
    // Multiplier scaling: gain = (volume * mult * 1024) / 10000
    int32_t gain = ((int32_t)st->volume * (int32_t)st->mult * 1024) / 10000;
    int16_t chunk_buf[128];
    const uint8_t *src_bytes = (const uint8_t *)data;
    size_t remaining = size;

    if (st->bits_per_sample == 16) {
      while (remaining > 0) {
        size_t chunk_bytes =
            (remaining > sizeof(chunk_buf)) ? sizeof(chunk_buf) : remaining;
        size_t samples = chunk_bytes / sizeof(int16_t);
        const int16_t *src16 = (const int16_t *)src_bytes;

        for (size_t i = 0; i < samples; i++) {
          chunk_buf[i] = (int16_t)(((int32_t)src16[i] * gain) >> 10);
        }

        size_t written = 0;
        err = i2s_channel_write(st->tx_chan, chunk_buf,
                                samples * sizeof(int16_t), &written,
                                pdMS_TO_TICKS(1000));
        if (err != ESP_OK)
          break;
        total_written += written;
        src_bytes += written;
        remaining -= written;
        if (written == 0)
          break;
      }
    } else {
      err = i2s_channel_write(st->tx_chan, data, size, &total_written,
                              pdMS_TO_TICKS(2000));
    }
  }

  xSemaphoreGive(st->lock);

  if (err != ESP_OK && total_written == 0) {
    errno = EIO;
    return -1;
  }

  return (ssize_t)total_written;
}

static int driver_sound_max98357_ioctl(eos_dev_t *dev, int fd, int cmd,
                                       va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }

  max98357_state_t *st = dev->state;
  if (xSemaphoreTake(st->lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  int ret = 0;
  switch (cmd) {
  case EOS_SOUND_IOCTL_GET_CONFIG: {
    eos_sound_config_t *cfg = va_arg(args, eos_sound_config_t *);
    if (!cfg) {
      errno = EINVAL;
      ret = -1;
    } else {
      cfg->sample_rate = st->sample_rate;
      cfg->bits_per_sample = (uint8_t)st->bits_per_sample;
      cfg->channels = (uint8_t)st->channels;
      cfg->volume = (uint8_t)st->volume;
      cfg->mult = (uint8_t)st->mult;
    }
    break;
  }
  case EOS_SOUND_IOCTL_SET_CONFIG: {
    const eos_sound_config_t *cfg = va_arg(args, const eos_sound_config_t *);
    if (!cfg || cfg->volume > 100 || cfg->mult > 100) {
      errno = EINVAL;
      ret = -1;
    } else {
      st->volume = cfg->volume;
      st->mult = cfg->mult;
      if (!max98357_apply_format(st, cfg->sample_rate, cfg->bits_per_sample,
                                cfg->channels)) {
        errno = EINVAL;
        ret = -1;
      }
    }
    break;
  }
  case EOS_SOUND_IOCTL_SET_SAMPLE_RATE: {
    uint32_t rate = va_arg(args, uint32_t);
    if (!max98357_apply_format(st, rate, st->bits_per_sample, st->channels)) {
      errno = EINVAL;
      ret = -1;
    }
    break;
  }
  case EOS_SOUND_IOCTL_GET_SAMPLE_RATE: {
    uint32_t *rate = va_arg(args, uint32_t *);
    if (!rate) {
      errno = EINVAL;
      ret = -1;
    } else {
      *rate = st->sample_rate;
    }
    break;
  }
  case EOS_SOUND_IOCTL_SET_BITS_PER_SAMPLE: {
    uint32_t bits = va_arg(args, uint32_t);
    if (!max98357_apply_format(st, st->sample_rate, bits, st->channels)) {
      errno = EINVAL;
      ret = -1;
    }
    break;
  }
  case EOS_SOUND_IOCTL_GET_BITS_PER_SAMPLE: {
    uint32_t *bits = va_arg(args, uint32_t *);
    if (!bits) {
      errno = EINVAL;
      ret = -1;
    } else {
      *bits = st->bits_per_sample;
    }
    break;
  }
  case EOS_SOUND_IOCTL_SET_CHANNELS: {
    uint32_t ch = va_arg(args, uint32_t);
    if (!max98357_apply_format(st, st->sample_rate, st->bits_per_sample, ch)) {
      errno = EINVAL;
      ret = -1;
    }
    break;
  }
  case EOS_SOUND_IOCTL_GET_CHANNELS: {
    uint32_t *ch = va_arg(args, uint32_t *);
    if (!ch) {
      errno = EINVAL;
      ret = -1;
    } else {
      *ch = st->channels;
    }
    break;
  }
  case EOS_SOUND_IOCTL_SET_VOLUME: {
    uint32_t vol = va_arg(args, uint32_t);
    if (vol > 100)
      vol = 100;
    st->volume = vol;
    break;
  }
  case EOS_SOUND_IOCTL_GET_VOLUME: {
    uint32_t *vol = va_arg(args, uint32_t *);
    if (!vol) {
      errno = EINVAL;
      ret = -1;
    } else {
      *vol = st->volume;
    }
    break;
  }
  case EOS_SOUND_IOCTL_SET_MULT: {
    uint32_t m = va_arg(args, uint32_t);
    if (m > 100)
      m = 100;
    st->mult = m;
    break;
  }
  case EOS_SOUND_IOCTL_GET_MULT: {
    uint32_t *m = va_arg(args, uint32_t *);
    if (!m) {
      errno = EINVAL;
      ret = -1;
    } else {
      *m = st->mult;
    }
    break;
  }
  case EOS_SOUND_IOCTL_START: {
    if (!st->enabled && st->tx_chan) {
      esp_err_t err = i2s_channel_enable(st->tx_chan);
      if (err == ESP_OK) {
        st->enabled = true;
      } else {
        errno = EIO;
        ret = -1;
      }
    }
    break;
  }
  case EOS_SOUND_IOCTL_STOP: {
    if (st->enabled && st->tx_chan) {
      esp_err_t err = i2s_channel_disable(st->tx_chan);
      if (err == ESP_OK) {
        st->enabled = false;
      } else {
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

EOS_DRV_ATTR eos_drv_t driver_sound_max98357 = {
    EOS_DRV_INIT,
    .scope = "sound",
    .name = "max98357",
    .devname = "sound",
    .init = driver_sound_max98357_init,
    .write = driver_sound_max98357_write,
    .ioctl = driver_sound_max98357_ioctl,
    .shutdown = driver_sound_max98357_shutdown,
};

#endif
