#include "includes.h"

#ifdef EOS_DRV_STORAGE_FLASH_ENABLED
#include <esp_flash.h>
#include <spi_flash_mmap.h>
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/error.h"
#include "ecore/ioctl.h"

#include <errno.h>

#define BAD_OFFSET -1

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

  state->chip = eos_cfg_get_ptr(dev->cfg, "chip", NULL);

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

int driver_storage_flash_write(eos_dev_t *dev, int fd, void *buf, size_t len) {
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

  flash_state_t *state = dev->state;

  if (!state) {
    errno = EINVAL;
    return BAD_OFFSET;
  }

  off_t max_offset = (off_t)state->size;

  switch (whence) {
  case SEEK_SET: {
    if (offset >= 0 && offset <= max_offset)
      state->offset = offset;
    else {
      errno = EINVAL;
      return BAD_OFFSET;
    }

    return state->offset;
  }

  case SEEK_CUR: {
    off_t new_offset = state->offset + offset;

    if (new_offset >= 0 && new_offset <= max_offset)
      state->offset = new_offset;
    else {
      errno = EINVAL;
      return BAD_OFFSET;
    }

    return state->offset;
  }

  case SEEK_END: {
    off_t new_offset = max_offset + offset;

    if (new_offset >= 0 && new_offset <= max_offset)
      state->offset = new_offset;
    else {
      errno = EINVAL;
      return BAD_OFFSET;
    }

    return state->offset;
  }

  default:
    errno = EINVAL;
    return BAD_OFFSET;
  }
}

// ── IOCTL ──────────────────────────────────────────────────────

int driver_storage_flash_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {

  flash_state_t *state = dev->state;

  if (!state)
    return EOS_ERR_DRIVER_INVALID_STATE;

  switch (cmd) {
  case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE: {
    uint32_t *sector_size = va_arg(args, uint32_t *);
    if (!sector_size)
      return EOS_ERR_INVALID_ARG;

    *sector_size = state->sector_size;
    break;
  }

  case EOS_STORAGE_IOCTL_GET_CAPACITY: {
    uint32_t *capacity = va_arg(args, uint32_t *);
    if (!capacity)
      return EOS_ERR_INVALID_ARG;

    *capacity = state->size;
    break;
  }

  default:
    return EOS_ERR_NOT_SUPPORTED;
  }

  return EOS_ERR_NO_ERROR;
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
