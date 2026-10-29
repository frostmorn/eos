#include "includes.h"
#ifdef EOS_DRV_BUS_SDIO_ENABLED

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include "soc/soc_caps.h"

#if SOC_SDMMC_HOST_SUPPORTED
#include <driver/sdmmc_default_configs.h>
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_types.h>
#include <sdmmc_cmd.h>
#endif

#if defined(SOC_SDMMC_IO_POWER_EXTERNAL) && SOC_SDMMC_IO_POWER_EXTERNAL
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#endif

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "edriver/bus/sdio/sdio.h"

#if SOC_SDMMC_HOST_SUPPORTED
esp_err_t eos_sdio_get_host(eos_dev_t *dev, sdmmc_host_t *out_host) {
  if (!dev || !dev->state || !out_host) return ESP_ERR_INVALID_ARG;
  sdio_state_t *st = (sdio_state_t *)dev->state;
  *out_host = st->host_cfg;
  return ESP_OK;
}

esp_err_t eos_sdio_get_card(eos_dev_t *dev, sdmmc_card_t **out_card) {
  if (!dev || !dev->state || !out_card) return ESP_ERR_INVALID_ARG;
  sdio_state_t *st = (sdio_state_t *)dev->state;
  if (!st->card_probed) return ESP_ERR_NOT_FOUND;
  *out_card = &st->card;
  return ESP_OK;
}

esp_err_t eos_sdio_probe_card(eos_dev_t *dev, sdmmc_card_t *out_card) {
  if (!dev || !dev->state) return ESP_ERR_INVALID_ARG;
  sdio_state_t *st = (sdio_state_t *)dev->state;
  if (!st->slot_initialized) return ESP_ERR_INVALID_STATE;

  if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t err = sdmmc_card_init(&st->host_cfg, &st->card);
  if (err == ESP_OK) {
    st->card_probed = true;
    if (out_card) *out_card = st->card;
    EOS_LOGI("sdio: slot %d card probed successfully (%s, %s)",
             st->slot,
             st->card.is_sdio ? "SDIO" : (st->card.is_mmc ? "MMC" : "SD"),
             st->card.cid.name);
  }
  xSemaphoreGive(st->mutex);
  return err;
}
#endif

esp_err_t eos_sdio_reset_slave(eos_dev_t *dev) {
  if (!dev || !dev->state) return ESP_ERR_INVALID_ARG;
  sdio_state_t *st = (sdio_state_t *)dev->state;
  if (st->rst_pin < 0) return ESP_ERR_NOT_SUPPORTED;

  gpio_set_level((gpio_num_t)st->rst_pin, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level((gpio_num_t)st->rst_pin, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
  return ESP_OK;
}

bool driver_bus_sdio_init(eos_dev_t *dev) {
  sdio_state_t *st = calloc(1, sizeof(sdio_state_t));
  if (!st) return false;

  st->mutex = xSemaphoreCreateMutex();
  if (!st->mutex) {
    free(st);
    return false;
  }

  st->slot = eos_cfg_get_i(dev->cfg, "slot", 0);
  st->width = eos_cfg_get_i(dev->cfg, "width", 4);
  st->freq_khz = eos_cfg_get_i(dev->cfg, "freq_khz", 20000);
  st->ldo_chan = eos_cfg_get_i(dev->cfg, "ldo_chan", -1);

  st->clk_pin = eos_pin_get_no(dev->pins, "clk");
  st->cmd_pin = eos_pin_get_no(dev->pins, "cmd");
  st->d0_pin = eos_pin_get_no(dev->pins, "d0");
  st->d1_pin = eos_pin_get_no(dev->pins, "d1");
  st->d2_pin = eos_pin_get_no(dev->pins, "d2");
  st->d3_pin = eos_pin_get_no(dev->pins, "d3");
  st->cd_pin = eos_pin_get_no(dev->pins, "cd");
  st->wp_pin = eos_pin_get_no(dev->pins, "wp");
  st->rst_pin = eos_pin_get_no(dev->pins, "rst");

  // Claim GPIO resources
  if (st->clk_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->clk_pin, dev);
  if (st->cmd_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->cmd_pin, dev);
  if (st->d0_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->d0_pin, dev);
  if (st->d1_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->d1_pin, dev);
  if (st->d2_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->d2_pin, dev);
  if (st->d3_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->d3_pin, dev);
  if (st->cd_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->cd_pin, dev);
  if (st->wp_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->wp_pin, dev);
  if (st->rst_pin >= 0) eos_cap_claim(EOS_CAPS_GPIO, st->rst_pin, dev);

  bool lazy_slot = (bool)eos_cfg_get_i(dev->cfg, "lazy_slot", 0);

  // If reset pin is present and slot is not lazy/external, pulse reset
  if (!lazy_slot && st->rst_pin >= 0) {
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << st->rst_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);
    gpio_set_level((gpio_num_t)st->rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)st->rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    EOS_LOGI("sdio: reset pulsed on GPIO %d (slot %d)", st->rst_pin, st->slot);
  }

#if SOC_SDMMC_HOST_SUPPORTED
#if defined(SOC_SDMMC_IO_POWER_EXTERNAL) && SOC_SDMMC_IO_POWER_EXTERNAL
  if (st->ldo_chan >= 0) {
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = st->ldo_chan,
    };
    esp_err_t pwr_err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &st->pwr_ctrl_handle);
    if (pwr_err == ESP_OK) {
      EOS_LOGI("sdio: on-chip LDO channel %d initialized for slot %d", st->ldo_chan, st->slot);
    } else {
      EOS_LOGW("sdio: failed to initialize on-chip LDO: %s", esp_err_to_name(pwr_err));
    }
  }
#endif

  st->host_cfg = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
  st->host_cfg.slot = st->slot;
  if (st->freq_khz > 0) {
    st->host_cfg.max_freq_khz = st->freq_khz;
  }
#if defined(SOC_SDMMC_IO_POWER_EXTERNAL) && SOC_SDMMC_IO_POWER_EXTERNAL
  if (st->pwr_ctrl_handle) {
    st->host_cfg.pwr_ctrl_handle = st->pwr_ctrl_handle;
  }
#endif

  sdmmc_host_state_t hstate = {0};
  if (sdmmc_host_get_state(&hstate) == ESP_OK && hstate.host_initialized) {
    st->host_initialized = true;
  } else {
    esp_err_t host_err = sdmmc_host_init();
    if (host_err != ESP_OK && host_err != ESP_ERR_INVALID_STATE && host_err != ESP_ERR_NOT_FOUND) {
      EOS_LOGE("sdio: sdmmc_host_init failed: %s", esp_err_to_name(host_err));
    } else {
      st->host_initialized = true;
    }
  }

  st->slot_cfg = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
  st->slot_cfg.width = st->width;
  if (st->clk_pin >= 0) st->slot_cfg.clk = (gpio_num_t)st->clk_pin;
  if (st->cmd_pin >= 0) st->slot_cfg.cmd = (gpio_num_t)st->cmd_pin;
  if (st->d0_pin >= 0) st->slot_cfg.d0 = (gpio_num_t)st->d0_pin;
  if (st->d1_pin >= 0) st->slot_cfg.d1 = (gpio_num_t)st->d1_pin;
  if (st->d2_pin >= 0) st->slot_cfg.d2 = (gpio_num_t)st->d2_pin;
  if (st->d3_pin >= 0) st->slot_cfg.d3 = (gpio_num_t)st->d3_pin;
  if (st->cd_pin >= 0) st->slot_cfg.cd = (gpio_num_t)st->cd_pin;
  if (st->wp_pin >= 0) st->slot_cfg.wp = (gpio_num_t)st->wp_pin;

  if (!lazy_slot) {
    esp_err_t slot_err = sdmmc_host_init_slot(st->slot, &st->slot_cfg);
    if (slot_err != ESP_OK && slot_err != ESP_ERR_INVALID_STATE) {
      EOS_LOGW("sdio: sdmmc_host_init_slot %d failed: %s", st->slot, esp_err_to_name(slot_err));
    } else {
      st->slot_initialized = true;
      EOS_LOGI("sdio: slot %d initialized (%d-bit, %lu kHz)",
               st->slot, st->width, (unsigned long)st->freq_khz);
    }
  } else {
    EOS_LOGI("sdio: slot %d configured (lazy/coprocessor mode)", st->slot);
  }
#endif

  dev->state = st;
  return true;
}

void driver_bus_sdio_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  sdio_state_t *st = (sdio_state_t *)dev->state;

#if SOC_SDMMC_HOST_SUPPORTED
  if (st->slot_initialized) {
    sdmmc_host_deinit_slot(st->slot);
    st->slot_initialized = false;
  }
#if defined(SOC_SDMMC_IO_POWER_EXTERNAL) && SOC_SDMMC_IO_POWER_EXTERNAL
  if (st->pwr_ctrl_handle) {
    sd_pwr_ctrl_del_on_chip_ldo(st->pwr_ctrl_handle);
    st->pwr_ctrl_handle = NULL;
  }
#endif
#endif

  if (st->clk_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->clk_pin, dev);
  if (st->cmd_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->cmd_pin, dev);
  if (st->d0_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->d0_pin, dev);
  if (st->d1_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->d1_pin, dev);
  if (st->d2_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->d2_pin, dev);
  if (st->d3_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->d3_pin, dev);
  if (st->cd_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->cd_pin, dev);
  if (st->wp_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->wp_pin, dev);
  if (st->rst_pin >= 0) eos_cap_release(EOS_CAPS_GPIO, st->rst_pin, dev);

  if (st->mutex) {
    vSemaphoreDelete(st->mutex);
  }

  free(st);
  dev->state = NULL;
}

bool driver_bus_sdio_attach_req(eos_dev_t *dev, eos_dev_t *child) {
  (void)child;
  if (!dev || !dev->state) return false;
  sdio_state_t *st = (sdio_state_t *)dev->state;

  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  st->attached_count++;
  xSemaphoreGive(st->mutex);
  return true;
}

bool driver_bus_sdio_detach_req(eos_dev_t *dev, eos_dev_t *child) {
  (void)child;
  if (!dev || !dev->state) return false;
  sdio_state_t *st = (sdio_state_t *)dev->state;

  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  if (st->attached_count > 0) {
    st->attached_count--;
  }
  xSemaphoreGive(st->mutex);
  return true;
}

int driver_bus_sdio_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  return dev ? EOS_DRV_FD : -1;
}

int driver_bus_sdio_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

int driver_bus_sdio_fstat(eos_dev_t *dev, int fd, struct stat *st) {
  (void)dev;
  (void)fd;
  if (!st) return -1;
  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFCHR | 0666;
  return 0;
}

ssize_t driver_bus_sdio_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) return -1;
  sdio_state_t *st = (sdio_state_t *)dev->state;

#if SOC_SDMMC_HOST_SUPPORTED
  if (!st->card_probed) {
    if (sdmmc_card_init(&st->host_cfg, &st->card) == ESP_OK) {
      st->card_probed = true;
    } else {
      return -1;
    }
  }

  if (st->card.is_mem) {
    size_t sector_size = st->card.csd.sector_size ? st->card.csd.sector_size : 512;
    size_t cnt = size / sector_size;
    if (cnt == 0) return 0;
    if (sdmmc_read_sectors(&st->card, dst, st->offset, cnt) != ESP_OK) {
      return -1;
    }
    st->offset += cnt;
    return (ssize_t)(cnt * sector_size);
  }
#endif
  return 0;
}

ssize_t driver_bus_sdio_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) return -1;
  sdio_state_t *st = (sdio_state_t *)dev->state;

#if SOC_SDMMC_HOST_SUPPORTED
  if (!st->card_probed) return -1;
  if (st->card.is_mem) {
    size_t sector_size = st->card.csd.sector_size ? st->card.csd.sector_size : 512;
    size_t cnt = size / sector_size;
    if (cnt == 0) return 0;
    if (sdmmc_write_sectors(&st->card, data, st->offset, cnt) != ESP_OK) {
      return -1;
    }
    st->offset += cnt;
    return (ssize_t)(cnt * sector_size);
  }
#endif
  return 0;
}

off_t driver_bus_sdio_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
  (void)fd;
  if (!dev || !dev->state) return -1;
  sdio_state_t *st = (sdio_state_t *)dev->state;
  off_t max_offset = st->card_probed ? (off_t)st->card.csd.capacity : 0;

  switch (whence) {
    case SEEK_SET:
      if (offset >= 0 && (max_offset == 0 || offset <= max_offset)) {
        st->offset = offset;
        return st->offset;
      }
      break;
    case SEEK_CUR:
      if (st->offset + offset >= 0 && (max_offset == 0 || st->offset + offset <= max_offset)) {
        st->offset += offset;
        return st->offset;
      }
      break;
    case SEEK_END:
      if (max_offset + offset >= 0) {
        st->offset = max_offset + offset;
        return st->offset;
      }
      break;
    default:
      break;
  }
  errno = EINVAL;
  return -1;
}

int driver_bus_sdio_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = EINVAL;
    return -1;
  }
  sdio_state_t *st = (sdio_state_t *)dev->state;

  switch (cmd) {
    case EOS_SDIO_IOCTL_GET_HOST: {
#if SOC_SDMMC_HOST_SUPPORTED
      sdmmc_host_t *out = va_arg(args, sdmmc_host_t *);
      if (!out) { errno = EINVAL; return -1; }
      *out = st->host_cfg;
      return 0;
#else
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_GET_SLOT: {
      int *slot = va_arg(args, int *);
      if (!slot) { errno = EINVAL; return -1; }
      *slot = st->slot;
      return 0;
    }
    case EOS_SDIO_IOCTL_SET_CLOCK_HZ: {
      uint32_t freq_hz = va_arg(args, uint32_t);
#if SOC_SDMMC_HOST_SUPPORTED
      esp_err_t err = sdmmc_host_set_card_clk(st->slot, freq_hz / 1000);
      return (err == ESP_OK) ? 0 : -1;
#else
      (void)freq_hz;
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_SET_BUS_WIDTH: {
      uint32_t width = va_arg(args, uint32_t);
#if SOC_SDMMC_HOST_SUPPORTED
      esp_err_t err = sdmmc_host_set_bus_width(st->slot, width);
      return (err == ESP_OK) ? 0 : -1;
#else
      (void)width;
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_RESET: {
      return (eos_sdio_reset_slave(dev) == ESP_OK) ? 0 : -1;
    }
    case EOS_SDIO_IOCTL_GET_CARD: {
#if SOC_SDMMC_HOST_SUPPORTED
      sdmmc_card_t **out = va_arg(args, sdmmc_card_t **);
      if (!out) { errno = EINVAL; return -1; }
      return (eos_sdio_get_card(dev, out) == ESP_OK) ? 0 : -1;
#else
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_PROBE_CARD: {
#if SOC_SDMMC_HOST_SUPPORTED
      sdmmc_card_t *out = va_arg(args, sdmmc_card_t *);
      return (eos_sdio_probe_card(dev, out) == ESP_OK) ? 0 : -1;
#else
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_IO_READ_BYTE: {
#if SOC_SDMMC_HOST_SUPPORTED
      eos_sdio_cmd52_t *c = va_arg(args, eos_sdio_cmd52_t *);
      if (!c) { errno = EINVAL; return -1; }
      if (!st->card_probed) return -1;
      esp_err_t err = sdmmc_io_read_byte(&st->card, c->function, c->reg, &c->read_val);
      return (err == ESP_OK) ? 0 : -1;
#else
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_IO_WRITE_BYTE: {
#if SOC_SDMMC_HOST_SUPPORTED
      eos_sdio_cmd52_t *c = va_arg(args, eos_sdio_cmd52_t *);
      if (!c) { errno = EINVAL; return -1; }
      if (!st->card_probed) return -1;
      esp_err_t err = sdmmc_io_write_byte(&st->card, c->function, c->reg, c->write_val, &c->read_val);
      return (err == ESP_OK) ? 0 : -1;
#else
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_IO_READ_BYTES: {
#if SOC_SDMMC_HOST_SUPPORTED
      eos_sdio_cmd53_t *c = va_arg(args, eos_sdio_cmd53_t *);
      if (!c || !c->buf) { errno = EINVAL; return -1; }
      if (!st->card_probed) return -1;
      uint32_t addr = c->addr | (c->fixed_addr ? SDMMC_IO_FIXED_ADDR : 0);
      esp_err_t err = sdmmc_io_read_bytes(&st->card, c->function, addr, c->buf, c->len);
      return (err == ESP_OK) ? 0 : -1;
#else
      errno = ENOTSUP;
      return -1;
#endif
    }
    case EOS_SDIO_IOCTL_IO_WRITE_BYTES: {
#if SOC_SDMMC_HOST_SUPPORTED
      eos_sdio_cmd53_t *c = va_arg(args, eos_sdio_cmd53_t *);
      if (!c || !c->buf) { errno = EINVAL; return -1; }
      if (!st->card_probed) return -1;
      uint32_t addr = c->addr | (c->fixed_addr ? SDMMC_IO_FIXED_ADDR : 0);
      esp_err_t err = sdmmc_io_write_bytes(&st->card, c->function, addr, c->buf, c->len);
      return (err == ESP_OK) ? 0 : -1;
#else
      errno = ENOTSUP;
      return -1;
#endif
    }
    default:
      errno = EINVAL;
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_bus_sdio = {
    EOS_DRV_INIT,
    .scope = "bus",
    .name = "sdio",
    .devname = "sdio",
    .init = driver_bus_sdio_init,
    .shutdown = driver_bus_sdio_shutdown,
    .attach_req = driver_bus_sdio_attach_req,
    .detach_req = driver_bus_sdio_detach_req,
    .open = driver_bus_sdio_open,
    .close = driver_bus_sdio_close,
    .fstat = driver_bus_sdio_fstat,
    .read = driver_bus_sdio_read,
    .write = driver_bus_sdio_write,
    .lseek = driver_bus_sdio_lseek,
    .ioctl = driver_bus_sdio_ioctl,
};

#endif
