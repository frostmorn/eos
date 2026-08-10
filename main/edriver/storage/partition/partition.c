#include "ecore/device.h"
#include "ecore/diskpart.h"
#include "ecore/driver.h"
#include "ecore/error.h"
#include "ecore/ioctl.h"
#include "includes.h"

#ifdef EOS_DRIVER_STORAGE_PARTITION_ENABLED

// ── State ─────────────────────────────────────────────────────
// set by parent SD driver before attach, partition owns it after

typedef struct {
  eos_part_t *info; // lba_start, lba_size, type
  off_t offset;     // current position in sectors relative to partition start
  uint32_t sector_size; // cached from parent
} partition_state_t;

// ── Init / Shutdown ───────────────────────────────────────────

bool driver_storage_partition_init(eos_dev_t *dev) {
  dev->state = malloc(sizeof(partition_state_t));

  memset(dev->state, 0, sizeof(partition_state_t));

  if (!dev->state)
    return false;

  partition_state_t *state = dev->state;

  state->offset = 0;

  eos_part_t *part = eos_cfg_get_ptr(dev->cfg, "part", NULL);
  if (!part)
    return false;

  state->info = part;

  // get sector size from parent
  uint32_t sector_size = 512; // default
  // actually use direct state access since parent is SD
  state->sector_size = sector_size;

  EOS_LOGI("partition: %s type=%s lba_start=%lu lba_size=%lu", dev->name,
           eos_part_type_str(state->info->part_type),
           (unsigned long)state->info->lba_start,
           (unsigned long)state->info->lba_size);

  return true;
}

void driver_storage_partition_shutdown(eos_dev_t *dev) {
  if (dev->state) {
    free(dev->state);
    dev->state = NULL;
  }
}

// ── IO ────────────────────────────────────────────────────────
int driver_storage_partition_read(eos_dev_t *dev, void *buf, size_t len) {
  partition_state_t *state = dev->state;
  if (!state || !dev->parent)
    return -1;

  uint32_t sectors = len / state->sector_size;
  off_t max = (off_t)state->info->lba_size;

  // check current position is within partition
  if (state->offset >= max)
    return -1;

  // clamp to partition boundary
  if (state->offset + sectors > max)
    sectors = (uint32_t)(max - state->offset);

  size_t clamped_len = sectors * state->sector_size;

  off_t abs_lba = (off_t)state->info->lba_start + state->offset;
  if (dev->parent->driver->lseek(dev->parent, abs_lba, SEEK_SET) != abs_lba)
    return -1;

  int ret = dev->parent->driver->read(dev->parent, buf, clamped_len);
  if (ret > 0)
    state->offset += ret / state->sector_size;

  return ret;
}

int driver_storage_partition_write(eos_dev_t *dev, void *buf, size_t len) {
  partition_state_t *state = dev->state;
  if (!state || !dev->parent)
    return -1;

  uint32_t sectors = len / state->sector_size;
  off_t max = (off_t)state->info->lba_size;

  if (state->offset >= max)
    return -1;

  // clamp to partition boundary
  if (state->offset + sectors > max)
    sectors = (uint32_t)(max - state->offset);

  size_t clamped_len = sectors * state->sector_size;

  off_t abs_lba = (off_t)state->info->lba_start + state->offset;
  if (dev->parent->driver->lseek(dev->parent, abs_lba, SEEK_SET) != abs_lba)
    return -1;

  int ret = dev->parent->driver->write(dev->parent, buf, clamped_len);
  if (ret > 0)
    state->offset += ret / state->sector_size;

  return ret;
}

off_t driver_storage_partition_lseek(eos_dev_t *dev, off_t offset, int whence) {
  partition_state_t *state = dev->state;
  if (!state)
    return -1;

  off_t max = (off_t)state->info->lba_size;
  off_t new_offset;

  switch (whence) {
  case SEEK_SET:
    new_offset = offset;
    break;
  case SEEK_CUR:
    new_offset = state->offset + offset;
    break;
  case SEEK_END:
    new_offset = max + offset;
    break;
  default:
    return -1;
  }

  if (new_offset < 0 || new_offset > max)
    return -1;
  state->offset = new_offset;
  return state->offset;
}

int driver_storage_partition_ioctl(eos_dev_t *dev, int cmd, va_list args) {
  partition_state_t *state = dev->state;

  switch (cmd) {
  case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE: {
    uint32_t *out = va_arg(args, uint32_t *);
    *out = state->sector_size;
    return EOS_ERR_NO_ERROR;
  }
  case EOS_STORAGE_IOCTL_GET_CAPACITY: {
    uint32_t *out = va_arg(args, uint32_t *);
    *out = state->info->lba_size;
    return EOS_ERR_NO_ERROR;
  }
  }
  return -1;
}

EOS_DRIVER_ATTR eos_driver_t driver_storage_partition = {
    EOS_DRIVER_INIT,
    .scope = "storage",
    .name = "partition",
    .devname = "part",
    .init = driver_storage_partition_init,
    .shutdown = driver_storage_partition_shutdown,
    .read = driver_storage_partition_read,
    .write = driver_storage_partition_write,
    .lseek = driver_storage_partition_lseek,
    .ioctl = driver_storage_partition_ioctl,
};

#endif
