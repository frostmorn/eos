#include "includes.h"
#ifdef EOS_DRIVER_STORAGE_SD_ENABLED

#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <sdmmc_cmd.h>

#include "ecore/capsmgr.h"
#include "ecore/diskpart.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

#include <errno.h>

#define BAD_OFFSET -1

// EOS_STORAGE_IOCTL_GET_SECTOR_SIZE // uint32_t *sector_size

// ── State ─────────────────────────────────────────────────────

typedef struct {
  sdmmc_card_t card;
  char *io_buff;
  sdspi_dev_handle_t handle;
  spi_host_device_t host;
  eos_part_table_t part_table;
  off_t offset;
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
    abort();

  // Print card info
  sdmmc_card_print_info(stdout, &state->card);

  // TODO: Filesystem detection
  eos_diskpart_parse(dev, &state->part_table);

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

  uint8_t *dst = buf;
  size_t remaining = len;

  size_t sector_size = state->card.csd.sector_size;

  while (remaining) {
    uint64_t sector = state->offset / sector_size;
    size_t sector_off = state->offset % sector_size;

    /*
     * Fast path: whole aligned sectors
     */
    if (sector_off == 0 && remaining >= sector_size) {
      size_t sectors = remaining / sector_size;

      if (sdmmc_read_sectors(&state->card, dst, sector, sectors) != ESP_OK)
        return -1;

      size_t bytes = sectors * sector_size;

      dst += bytes;
      remaining -= bytes;
      state->offset += bytes;
      continue;
    }

    /*
     * Slow path: partial sector
     */
    if (sdmmc_read_sectors(&state->card, state->io_buff, sector, 1) != ESP_OK)
      return -1;

    size_t copy = sector_size - sector_off;
    if (copy > remaining)
      copy = remaining;

    memcpy(dst, state->io_buff + sector_off, copy);

    dst += copy;
    remaining -= copy;
    state->offset += copy;
  }

  return len;
}

int driver_storage_sd_write(eos_dev_t *dev, void *buf, size_t len) {
  sd_state_t *state = dev->state;
  if (!state)
    return -1;

  const uint8_t *src = buf;
  size_t remaining = len;

  size_t sector_size = state->card.csd.sector_size;

  while (remaining) {
    uint64_t sector = state->offset / sector_size;
    size_t sector_off = state->offset % sector_size;

    /*
     * Fast path: whole aligned sectors
     */
    if (sector_off == 0 && remaining >= sector_size) {
      size_t sectors = remaining / sector_size;

      if (sdmmc_write_sectors(&state->card, src, sector, sectors) != ESP_OK)
        return -1;

      size_t bytes = sectors * sector_size;

      src += bytes;
      remaining -= bytes;
      state->offset += bytes;
      continue;
    }

    /*
     * Partial sector: read-modify-write
     */
    if (sdmmc_read_sectors(&state->card, state->io_buff, sector, 1) != ESP_OK)
      return -1;

    size_t copy = sector_size - sector_off;
    if (copy > remaining)
      copy = remaining;

    memcpy(state->io_buff + sector_off, src, copy);

    if (sdmmc_write_sectors(&state->card, state->io_buff, sector, 1) != ESP_OK)
      return -1;

    src += copy;
    remaining -= copy;
    state->offset += copy;
  }

  return len;
}

off_t driver_storage_sd_lseek(eos_dev_t *dev, off_t offset, int whence) {
  sd_state_t *state = dev->state;

  if (!state) {
    errno = EINVAL;
    return BAD_OFFSET;
  }

  // TODO: off_t is a 32 bit signed value, so it means, I can't represent a
  // whole filesystem as a file, which is heartbreaking
  EOS_LOGI("sizeof(off_t) = %u", (unsigned)sizeof(off_t));
  EOS_LOGI("sizeof(long) = %u", (unsigned)sizeof(long));
  EOS_LOGI("sizeof(long long) = %u", (unsigned)sizeof(long long));

  off_t max_offset =
      (off_t)state->card.csd.capacity * (off_t)state->card.csd.sector_size;

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
    if (new_offset >= 0 && offset <= max_offset)
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

int driver_storage_sd_ioctl(eos_dev_t *dev, int cmd, ...) {
  sd_state_t *state = dev->state;
  va_list args;
  va_start(args, cmd);
  int ret = 0;

  switch (cmd) {
  case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE:
    uint32_t *sector_size = va_arg(args, uint32_t *);
    *sector_size = state->card.csd.sector_size;
    return ret;
  case EOS_STORAGE_IOCTL_GET_CAPACITY:
    uint32_t *capacity = va_arg(args, uint32_t *);
    *capacity = state->card.csd.capacity;
  }

  va_end(args);
  return ret;
}

EOS_DRIVER_ATTR eos_driver_t driver_storage_sd = {
    EOS_DRIVER_INIT,
    .scope = "storage",
    .name = "sd",
    .lseek = driver_storage_sd_lseek,
    .init = driver_storage_sd_init,
    .write = driver_storage_sd_write,
    .read = driver_storage_sd_read,
    .shutdown = driver_storage_sd_shutdown};

#endif
