#include "includes.h"

#ifdef EOS_DRV_STORAGE_FLASH_ENABLED
#include <esp_flash.h>
#include <spi_flash_mmap.h>
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

#include <errno.h>

// ── State ─────────────────────────────────────────────────────

typedef struct {
  esp_flash_t *chip;
  uint32_t size;
  uint32_t sector_size;
  off_t offset;
} flash_state_t;

// ── Init / Shutdown ───────────────────────────────────────────

bool driver_storage_flash_init(eos_dev_t *dev) {
  flash_state_t *state = malloc(sizeof(flash_state_t));
  if (!state)
    return false;

  memset(state, 0, sizeof(flash_state_t));
  dev->state = state;

  state->chip = eos_cfg_get_ptr(dev->cfg, "chip", esp_flash_default_chip);

  if (state->chip == NULL) {
    free(state);
    dev->state = NULL;
    return false;
  }

  /*
   * Use the physical flash size rather than the size encoded in
   * the application image.
   */
  if (esp_flash_get_physical_size(state->chip, &state->size) != ESP_OK) {
    free(state);
    dev->state = NULL;
    return false;
  }

  state->sector_size = SPI_FLASH_SEC_SIZE;

  EOS_LOGI("Flash: size=%lu sector_size=%lu", (unsigned long)state->size,
           (unsigned long)state->sector_size);

  return true;
}

void driver_storage_flash_shutdown(eos_dev_t *dev) {
  flash_state_t *state = dev->state;
  if (!state)
    return;

  /*
   * esp_flash_default_chip is owned and initialized by ESP-IDF.
   * EOS must not deinitialize or free it.
   */
  free(state);
  dev->state = NULL;
}

// ── IO — raw flash operations ─────────────────────────────────

int driver_storage_flash_read(eos_dev_t *dev, int fd, void *buf, size_t len) {
  flash_state_t *state = dev->state;

  if (!state || !buf)
    return -1;

  if (state->offset > state->size || len > state->size - state->offset) {
    errno = EINVAL;
    return -1;
  }

  esp_err_t err = esp_flash_read(state->chip, buf, state->offset, len);

  if (err != ESP_OK)
    return -1;

  state->offset += len;

  return len;
}

int driver_storage_flash_write(eos_dev_t *dev, int fd, const void *buf, size_t len) {
  flash_state_t *state = dev->state;

  if (!state || !buf)
    return -1;

  if (state->offset > state->size || len > state->size - state->offset) {
    errno = EINVAL;
    return -1;
  }

  esp_err_t err = esp_flash_write(state->chip, buf, state->offset, len);

  if (err != ESP_OK)
    return -1;

  state->offset += len;

  return len;
}

off_t driver_storage_flash_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
  (void)fd;
  flash_state_t *state = dev->state;

  if (!state || state->sector_size == 0) {
    errno = EINVAL;
    return BAD_OFFSET;
  }

  off_t max_sectors = (off_t)(state->size / state->sector_size);
  off_t cur_sector = (off_t)(state->offset / state->sector_size);
  off_t target_sector = 0;

  switch (whence) {
  case SEEK_SET:
    target_sector = offset;
    break;
  case SEEK_CUR:
    target_sector = cur_sector + offset;
    break;
  case SEEK_END:
    target_sector = max_sectors + offset;
    break;
  default:
    errno = EINVAL;
    return BAD_OFFSET;
  }

  if (target_sector < 0 || target_sector > max_sectors) {
    errno = EINVAL;
    return BAD_OFFSET;
  }

  state->offset = (size_t)target_sector * state->sector_size;
  return target_sector;
}

// ── IOCTL ──────────────────────────────────────────────────────

int driver_storage_flash_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {

  flash_state_t *state = dev->state;

  if (!state) {
    errno = EINVAL;
    return -1;
  }

  switch (cmd) {
  case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE: {
    uint32_t *sector_size = va_arg(args, uint32_t *);
    if (!sector_size) {
      errno = EINVAL;
      return -1;
    }

    *sector_size = state->sector_size;
    break;
  }

  case EOS_STORAGE_IOCTL_GET_CAPACITY: {
    uint32_t *capacity = va_arg(args, uint32_t *);
    if (!capacity) {
      errno = EINVAL;
      return -1;
    }

    *capacity = (state->sector_size > 0) ? (uint32_t)(state->size / state->sector_size) : 0;
    break;
  }

  default:
    errno = ENOTSUP;
    return -1;
  }

  return 0;
}

// ── Driver ─────────────────────────────────────────────────────

EOS_DRV_ATTR eos_drv_t driver_storage_flash = {
    EOS_DRV_INIT,
    .scope = "storage",
    .name = "flash",
    .devname = "flash",
    .init = driver_storage_flash_init,
    .read = driver_storage_flash_read,
    .write = driver_storage_flash_write,
    .ioctl = driver_storage_flash_ioctl,
    .lseek = driver_storage_flash_lseek,
    .shutdown = driver_storage_flash_shutdown,
};

#endif
