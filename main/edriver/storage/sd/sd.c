#include "includes.h"
#ifdef EOS_DRIVER_STORAGE_SD_ENABLED

#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <sdmmc_cmd.h>

#include "ecore/capsmgr.h"
#include "ecore/diskpart.h"
#include "ecore/driver.h"
#include "ecore/error.h"
#include "ecore/ioctl.h"

#include <errno.h>

#define BAD_OFFSET -1

// ── State ─────────────────────────────────────────────────────

typedef struct {
  sdmmc_card_t card;
  char *io_buff;
  sdspi_dev_handle_t handle;
  spi_host_device_t host;
  eos_part_table_t part_table;
  off_t offset; // sector-based
} sd_state_t;

// ── Init / Shutdown ───────────────────────────────────────────

bool driver_storage_sd_init(eos_dev_t *dev) {
  sd_state_t *state = malloc(sizeof(sd_state_t));
  if (!state)
    return false;
  memset(state, 0, sizeof(sd_state_t));
  dev->state = state;

  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  if (cs_pin < 0 || !eos_cap_alloc(EOS_CAPS_GPIO, cs_pin, dev)) {
    free(state);
    return false;
  }

  state->host =
      (spi_host_device_t)eos_cfg_get_i(dev->parent->cfg, "host", SPI2_HOST);

  int32_t freq = eos_cfg_get_i(dev->cfg, "clock_speed_hz", SPI_MASTER_FREQ_20M);

  // Add SD card as SPI device
  sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev_cfg.host_id = state->host;
  dev_cfg.gpio_cs = cs_pin;

  if (sdspi_host_init_device(&dev_cfg, &state->handle) != ESP_OK) {
    eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);
    free(state);
    return false;
  }

  // Probe card
  sdmmc_host_t host_cfg = SDSPI_HOST_DEFAULT();
  host_cfg.slot = state->handle;
  host_cfg.max_freq_khz = freq / 1000;

  if (sdmmc_card_init(&host_cfg, &state->card) != ESP_OK) {
    sdspi_host_remove_device(state->handle);
    eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);
    free(state);
    return false;
  }

  // Allocate file buffer
  state->io_buff = (char *)malloc(state->card.csd.sector_size);

  // Gracefully die in case no mem :D
  if (!state->io_buff)
    return false;

  // Print card info
  sdmmc_card_print_info(stdout, &state->card);

  // Read partition table
  if (eos_diskpart_parse(dev, &state->part_table) != EOS_ERR_NO_ERROR){
    EOS_LOGE("Something happened during partition table reading\n");
    return false;
  }

  // Find generic partition driver
  eos_driver_t *drv = eos_driver_find("storage", "partition");
  
  if (drv == NULL){
    EOS_LOGE("Can't find a generic partition driver\n");
    return false;
  }

  // Attach partitions to device tree
  for (uint32_t i = 0; i < state->part_table.count; i++){
    eos_dev_t *part_dev = eos_dev_alloc();

    if (part_dev == NULL)
    {
      EOS_LOGE("Can't attach partition to dev tree. Device not allocated\n");
    }

    part_dev->driver = drv;
    eos_dev_attach(part_dev, dev);
  }

  return true;
}

void driver_storage_sd_shutdown(eos_dev_t *dev) {
  sd_state_t *state = dev->state;
  if (!state)
    return;

  if (state->handle)
    sdspi_host_remove_device(state->handle);

  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  if (cs_pin >= 0)
    eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);

  // Cleanup
  if (state->io_buff)
    free(state->io_buff);

  free(state);
  dev->state = NULL;
}

// ── IO — raw block operations ─────────────────────────────────

int driver_storage_sd_read(eos_dev_t *dev, void *buf, size_t len) {
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

  if (ESP_OK !=
      sdmmc_read_sectors(&state->card, buf, state->offset, cntsectors))
    return -1;

  return len;
}

int driver_storage_sd_write(eos_dev_t *dev, void *buf, size_t len) {
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

  if (ESP_OK !=
      sdmmc_write_sectors(&state->card, buf, state->offset, cntsectors))
    return -1;

  return len;
}

off_t driver_storage_sd_lseek(eos_dev_t *dev, off_t offset, int whence) {
  sd_state_t *state = dev->state;

  if (!state) {
    errno = EINVAL;
    return BAD_OFFSET;
  }

  off_t max_offset = (off_t)state->card.csd.capacity;

  EOS_LOGI("sd_lseek: offset=%lld whence=%d capacity=%lu sector_size=%lu "
           "max_offset=%lld",
           (long long)offset, whence, (unsigned long)state->card.csd.capacity,
           (unsigned long)state->card.csd.sector_size, (long long)max_offset);

  switch (whence) {
  case SEEK_SET: {
    if (offset >= 0 && offset <= max_offset)
      state->offset = offset;
    else
      // TODO: errno setup?
      return BAD_OFFSET;

    return state->offset;
  }
  case SEEK_CUR: {
    off_t new_offset = state->offset + offset;

    // Scroll back available, huh?
    if (new_offset >= 0 && new_offset <= max_offset)
      state->offset = new_offset;
    else
      // TODO: errno setup?
      return BAD_OFFSET;

    return state->offset;
  }
  case SEEK_END: {
    off_t new_offset = max_offset + offset;

    if (new_offset >= 0 && new_offset <= max_offset)
      state->offset = new_offset;
    else
      return BAD_OFFSET;

    return state->offset;
  }

  // Note: Other whences?
  default: {
    errno = EINVAL;
    return BAD_OFFSET;
  }
  }
}

int driver_storage_sd_ioctl(eos_dev_t *dev, int cmd, va_list args) {
  sd_state_t *state = dev->state;

  int ret = EOS_ERR_NO_ERROR;

  switch (cmd) {
  case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE:
    uint32_t *sector_size = va_arg(args, uint32_t *);
    *sector_size = state->card.csd.sector_size;
    break;
  case EOS_STORAGE_IOCTL_GET_CAPACITY:
    uint32_t *capacity = va_arg(args, uint32_t *);
    *capacity = state->card.csd.capacity;
    break;
  }

  return ret;
}

EOS_DRIVER_ATTR eos_driver_t driver_storage_sd = {
    EOS_DRIVER_INIT,
    .scope = "storage",
    .name = "sd",
    .lseek = driver_storage_sd_lseek,
    .ioctl = driver_storage_sd_ioctl,
    .init = driver_storage_sd_init,
    .write = driver_storage_sd_write,
    .read = driver_storage_sd_read,
    .shutdown = driver_storage_sd_shutdown};

#endif
