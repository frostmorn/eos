#include "includes.h"
#ifdef EOS_DRV_STORAGE_SD_ENABLED

#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <sdmmc_cmd.h>
#if SOC_SDMMC_HOST_SUPPORTED
#include <driver/sdmmc_default_configs.h>
#include <driver/sdmmc_host.h>
#endif

#include "ecore/capsmgr.h"
#include "ecore/diskpart.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"

#include <errno.h>

// ── State ─────────────────────────────────────────────────────

typedef struct {
  sdmmc_card_t card;
  char *io_buff;
  bool is_sdio;
  sdspi_dev_handle_t handle;
  spi_host_device_t host;
  eos_part_table_t part_table;
  off_t offset; // sector-based
  SemaphoreHandle_t io_mutex; // serializes seek + sector I/O on card
} sd_state_t;

// ── Init / Shutdown ───────────────────────────────────────────

void driver_storage_sd_shutdown(eos_dev_t *dev);

#if SOC_SDMMC_HOST_SUPPORTED
static bool init_sd_card_sdio(eos_dev_t *dev, sd_state_t *state) {
  sdmmc_host_t host_cfg = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
  int slot = eos_cfg_get_i(dev->parent->cfg, "slot", 0);
  host_cfg.slot = slot;

  int32_t freq = eos_cfg_get_i(dev->cfg, "clock_speed_hz", 40000000);
  if (freq > 0) {
    host_cfg.max_freq_khz = freq / EOS_MS_PER_SEC;
  }

  if (dev->parent) {
    if (eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_SDIO_IOCTL_GET_HOST, &host_cfg) == 0) {
      if (freq > 0) {
        host_cfg.max_freq_khz = freq / EOS_MS_PER_SEC;
      }
    }
  }

  if (sdmmc_card_init(&host_cfg, &state->card) != ESP_OK) {
    EOS_LOGW("sd: card initialization over SDIO (slot %d) failed", slot);
    return false;
  }
  return true;
}
#endif

static bool init_sd_card_spi(eos_dev_t *dev, sd_state_t *state) {
  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  if (cs_pin < 0 || !eos_cap_claim(EOS_CAPS_GPIO, cs_pin, dev)) {
    return false;
  }

  state->host = SPI2_HOST;
  if (dev->parent) {
    if (eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_SPI_IOCTL_GET_HOST, &state->host) != 0) {
      state->host =
          (spi_host_device_t)eos_cfg_get_i(dev->parent->cfg, "host", SPI2_HOST);
    }
  }

  int32_t freq = eos_cfg_get_i(dev->cfg, "clock_speed_hz", SPI_MASTER_FREQ_20M);

  sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev_cfg.host_id = state->host;
  dev_cfg.gpio_cs = cs_pin;

  if (sdspi_host_init_device(&dev_cfg, &state->handle) != ESP_OK) {
    return false;
  }

  sdmmc_host_t host_cfg = SDSPI_HOST_DEFAULT();
  host_cfg.slot = state->handle;
  host_cfg.max_freq_khz = freq / EOS_MS_PER_SEC;

  if (sdmmc_card_init(&host_cfg, &state->card) != ESP_OK) {
    return false;
  }
  return true;
}

static void attach_partitions(eos_dev_t *dev, sd_state_t *state) {
  eos_drv_t *drv = eos_drv_find("storage", "partition");
  if (drv == NULL) {
    EOS_LOGE("Can't find a generic partition driver\n");
    return;
  }

  for (uint32_t i = 0; i < state->part_table.count; i++) {
    eos_dev_t *part_dev = eos_dev_alloc();
    if (part_dev == NULL) {
      EOS_LOGE("Can't attach partition to dev tree. Device not allocated\n");
      continue;
    }

    eos_cfg_t cfg[] = {
        {
            .key = "part",
            .type = EOS_CFG_PTR,
            .val.ptr = &state->part_table.parts[i],
        },
        {0},
    };
    part_dev->driver = drv;
    part_dev->cfg = cfg;
    eos_dev_attach(part_dev, dev);
  }
}

bool driver_storage_sd_init(eos_dev_t *dev) {
  sd_state_t *state = eos_psram_malloc_prefer(sizeof(sd_state_t));
  if (!state)
    return false;
  memset(state, 0, sizeof(sd_state_t));
  dev->state = state;
  state->io_mutex = xSemaphoreCreateMutex();
  if (!state->io_mutex) {
    free(state);
    dev->state = NULL;
    return false;
  }

  bool is_sdio = false;
  if (dev->parent && dev->parent->driver &&
      strcmp(dev->parent->driver->scope, "bus") == 0 &&
      strcmp(dev->parent->driver->name, "sdio") == 0) {
    is_sdio = true;
  }
  state->is_sdio = is_sdio;

  if (is_sdio) {
#if SOC_SDMMC_HOST_SUPPORTED
    if (!init_sd_card_sdio(dev, state)) {
      driver_storage_sd_shutdown(dev);
      return false;
    }
#else
    EOS_LOGE("sd: SDMMC host not supported on this SoC");
    driver_storage_sd_shutdown(dev);
    return false;
#endif
  } else {
    if (!init_sd_card_spi(dev, state)) {
      driver_storage_sd_shutdown(dev);
      return false;
    }
  }

  state->io_buff = (char *)malloc(state->card.csd.sector_size);
  if (!state->io_buff) {
    driver_storage_sd_shutdown(dev);
    return false;
  }

  sdmmc_card_print_info(stdout, &state->card);

  if (eos_diskpart_parse(dev, &state->part_table) != 0) {
    EOS_LOGE("Something happened during partition table reading\n");
    driver_storage_sd_shutdown(dev);
    return false;
  }

  attach_partitions(dev, state);
  return true;
}

void driver_storage_sd_shutdown(eos_dev_t *dev) {
  sd_state_t *state = dev->state;
  if (!state)
    return;

  if (!state->is_sdio) {
    if (state->handle)
      sdspi_host_remove_device(state->handle);

    int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
    if (cs_pin >= 0)
      eos_cap_release(EOS_CAPS_GPIO, cs_pin, dev);
  }

  // Cleanup
  if (state->io_buff)
    free(state->io_buff);
  if (state->io_mutex)
    vSemaphoreDelete(state->io_mutex);

  free(state);
  dev->state = NULL;
}

// ── IO — raw block operations ─────────────────────────────────

int driver_storage_sd_read(eos_dev_t *dev, int fd, void *buf, size_t len) {
  (void)fd;
  sd_state_t *state = dev->state;
  if (!state)
    return -1;

  size_t sector_size = state->card.csd.sector_size;

  if (len % sector_size != 0) {
    EOS_LOGE("Reading unaligned block with length %d. Sector size is %d", len,
             sector_size);
    return -1;
  }

  size_t cntsectors = len / sector_size;

  if (state->io_mutex) xSemaphoreTake(state->io_mutex, portMAX_DELAY);
  esp_err_t err = sdmmc_read_sectors(&state->card, buf, state->offset, cntsectors);
  if (state->io_mutex) xSemaphoreGive(state->io_mutex);
  if (err != ESP_OK)
    return -1;

  return len;
}

int driver_storage_sd_write(eos_dev_t *dev, int fd, const void *buf, size_t len) {
  (void)fd;
  sd_state_t *state = dev->state;
  if (!state)
    return -1;
  size_t sector_size = state->card.csd.sector_size;

  if (len % sector_size != 0) {
    EOS_LOGE("Writing unaligned block with length %d. Sector size is %d", len,
             sector_size);
    return -1;
  }

  size_t cntsectors = len / sector_size;

  if (state->io_mutex) xSemaphoreTake(state->io_mutex, portMAX_DELAY);
  esp_err_t err = sdmmc_write_sectors(&state->card, buf, state->offset, cntsectors);
  if (state->io_mutex) xSemaphoreGive(state->io_mutex);
  if (err != ESP_OK) {
    EOS_LOGE("sd_write: sdmmc_write_sectors failed: %s (0x%x) offset=%lld, cnt=%zu",
             esp_err_to_name(err), err, (long long)state->offset, cntsectors);
    return -1;
  }

  return len;
}

off_t driver_storage_sd_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
  (void)fd;
  sd_state_t *state = dev->state;

  if (!state) {
    errno = EINVAL;
    return BAD_OFFSET;
  }

  if (state->io_mutex) xSemaphoreTake(state->io_mutex, portMAX_DELAY);
  off_t max_offset = (off_t)state->card.csd.capacity;
  off_t res = BAD_OFFSET;

  switch (whence) {
  case SEEK_SET: {
    if (offset >= 0 && offset <= max_offset) {
      state->offset = offset;
      res = state->offset;
    }
    break;
  }
  case SEEK_CUR: {
    off_t new_offset = state->offset + offset;
    if (new_offset >= 0 && new_offset <= max_offset) {
      state->offset = new_offset;
      res = state->offset;
    }
    break;
  }
  case SEEK_END: {
    off_t new_offset = max_offset + offset;
    if (new_offset >= 0 && new_offset <= max_offset) {
      state->offset = new_offset;
      res = state->offset;
    }
    break;
  }
  default:
    errno = EINVAL;
    break;
  }
  if (state->io_mutex) xSemaphoreGive(state->io_mutex);
  return res;
}

int driver_storage_sd_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  sd_state_t *state = dev ? dev->state : NULL;
  if (!state) {
    errno = EINVAL;
    return -1;
  }

  switch (cmd) {
  case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE: {
    uint32_t *sector_size = va_arg(args, uint32_t *);
    if (!sector_size) { errno = EINVAL; return -1; }
    *sector_size = state->card.csd.sector_size;
    return 0;
  }
  case EOS_STORAGE_IOCTL_GET_CAPACITY: {
    uint32_t *capacity = va_arg(args, uint32_t *);
    if (!capacity) { errno = EINVAL; return -1; }
    *capacity = state->card.csd.capacity;
    return 0;
  }
  case EOS_STORAGE_IOCTL_MOUNT: {
    const char *path = va_arg(args, const char *);
    (void)path;
    bool *result = va_arg(args, bool *);
    if (!result) { errno = EINVAL; return -1; }
    *result = false; // Not mountable
    return 0;
  }
  case EOS_STORAGE_IOCTL_UMOUNT: {
    bool *result = va_arg(args, bool *);
    if (!result) { errno = EINVAL; return -1; }
    *result = false; // Nothing to unmount
    return 0;
  }
  case EOS_STORAGE_IOCTL_GET_CARD: {
    void **card_ptr = va_arg(args, void **);
    if (!card_ptr) { errno = EINVAL; return -1; }
    *card_ptr = &state->card;
    return 0;
  }
  default:
    errno = ENOTTY;
    return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_storage_sd = {
    EOS_DRV_INIT,
    .scope = "storage",
    .name = "sd",
    .devname = "sd",
    .lseek = driver_storage_sd_lseek,
    .ioctl = driver_storage_sd_ioctl,
    .init = driver_storage_sd_init,
    .write = driver_storage_sd_write,
    .read = driver_storage_sd_read,
    .shutdown = driver_storage_sd_shutdown};

#endif
