#include <diskio.h>
#include <diskio_impl.h>
#include <esp_vfs_fat.h>
#include <ff.h>

#include "ecore/dev.h"
#include "ecore/diskpart.h"
#include "ecore/driver.h"
#include "ecore/error.h"
#include "ecore/ioctl.h"
#include "ecore/rootfs.h"
#include "includes.h"

#ifdef EOS_DRV_STORAGE_PARTITION_ENABLED

// ── State ─────────────────────────────────────────────────────

typedef struct {
  eos_part_info_t *info; // lba_start, lba_size, type
  off_t offset; // current position in sectors relative to partition start
  uint32_t sector_size; // cached from parent

  // FatFs mount bookkeeping — EOS_STORAGE_IOCTL_UMOUNT carries no path,
  // so whatever was used to mount has to be remembered here.
  bool fat_mounted;
  BYTE fat_pdrv;
  FATFS *fat_fs;
  char mount_path[EOS_SMALL_STR_LEN];
} partition_state_t;

// ── Init / Shutdown ───────────────────────────────────────────

bool driver_storage_partition_init(eos_dev_t *dev) {
  dev->state = malloc(sizeof(partition_state_t));

  memset(dev->state, 0, sizeof(partition_state_t));

  if (!dev->state)
    return false;

  partition_state_t *state = dev->state;

  state->offset = 0;

  eos_part_info_t *part = eos_cfg_get_ptr(dev->cfg, "part", NULL);
  if (!part)
    return false;

  state->info = part;

  // get sector size from parent
  uint32_t sector_size = 512; // fallback if parent can't tell us
  if (eos_dev_ioctl_call(dev->parent, EOS_DRV_FD, EOS_STORAGE_IOCTL_GET_SECTOR_SIZE,
                         &sector_size) != EOS_ERR_NO_ERROR) {
    EOS_LOGW("partition: %s failed to query parent sector size, "
             "defaulting to %lu",
             dev->name, (unsigned long)sector_size);
  }
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
int driver_storage_partition_read(eos_dev_t *dev, int fd, void *buf, size_t len) {
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
  if (eos_drv_lseek(dev->parent, EOS_DRV_FD, abs_lba, SEEK_SET) != abs_lba)
    return -1;

  int ret = eos_drv_read(dev->parent, EOS_DRV_FD, buf, clamped_len);
  if (ret > 0)
    state->offset += ret / state->sector_size;

  return ret;
}

int driver_storage_partition_write(eos_dev_t *dev, int fd, const void *buf, size_t len) {
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
  if (eos_drv_lseek(dev->parent, EOS_DRV_FD, abs_lba, SEEK_SET) != abs_lba)
    return -1;

  int ret = eos_drv_write(dev->parent, EOS_DRV_FD, buf, clamped_len);
  if (ret > 0)
    state->offset += ret / state->sector_size;

  return ret;
}

off_t driver_storage_partition_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
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

// ── FatFs disk IO glue ───────────────────────────────────────────
//
// Bridges FatFs's sector-oriented ff_diskio_impl_t callbacks
// (which only carry a bare pdrv number) back to the eos_dev_t/driver
// pair that owns that pdrv. Note: since driver_storage_partition_read/
// write/lseek already clamp everything to [lba_start, lba_start+lba_size),
// FatFs sees this pdrv as a clean, self-contained disk starting at
// sector 0 - no MBR needs to live inside the partition itself, plain
// auto-detect (VolToPart[pdrv] = {pdrv, 0}, the ESP-IDF default) is fine.
// TODO: try to store it in a global  mtab
static eos_dev_t *s_pdrv_map[FF_VOLUMES] = {NULL};

eos_dev_t *eos_diskpart_pdrv_to_dev(unsigned char pdrv) {
  if (pdrv >= FF_VOLUMES)
    return NULL;
  return s_pdrv_map[pdrv];
}

static DSTATUS eos_ff_disk_init(BYTE pdrv) {
  return s_pdrv_map[pdrv] ? 0 : STA_NOINIT;
}

static DSTATUS eos_ff_disk_status(BYTE pdrv) {
  return s_pdrv_map[pdrv] ? 0 : STA_NOINIT;
}

static DRESULT eos_ff_disk_read(BYTE pdrv, BYTE *buff, uint32_t sector,
                                unsigned count) {
  eos_dev_t *dev = s_pdrv_map[pdrv];
  if (!dev)
    return RES_NOTRDY;

  partition_state_t *state = dev->state;
  size_t len = (size_t)count * state->sector_size;

  if (eos_drv_lseek(dev, EOS_DRV_FD, (off_t)sector, SEEK_SET) != (off_t)sector)
    return RES_ERROR;

  return (eos_drv_read(dev, EOS_DRV_FD, buff, len) == (int)len) ? RES_OK : RES_ERROR;
}

static DRESULT eos_ff_disk_write(BYTE pdrv, const BYTE *buff, uint32_t sector,
                                 unsigned count) {
  eos_dev_t *dev = s_pdrv_map[pdrv];
  if (!dev)
    return RES_NOTRDY;

  partition_state_t *state = dev->state;
  size_t len = (size_t)count * state->sector_size;

  if (eos_drv_lseek(dev, EOS_DRV_FD, (off_t)sector, SEEK_SET) != (off_t)sector)
    return RES_ERROR;

  return (eos_drv_write(dev, EOS_DRV_FD, (void *)buff, len) == (int)len) ? RES_OK
                                                                  : RES_ERROR;
}

static DRESULT eos_ff_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  eos_dev_t *dev = s_pdrv_map[pdrv];
  if (!dev)
    return RES_NOTRDY;

  partition_state_t *state = dev->state;

  switch (cmd) {
  case CTRL_SYNC:
    return RES_OK;
  case GET_SECTOR_COUNT:
    *((DWORD *)buff) = state->info->lba_size;
    return RES_OK;
  case GET_SECTOR_SIZE:
    *((WORD *)buff) = (WORD)state->sector_size;
    return RES_OK;
  case GET_BLOCK_SIZE:
    *((DWORD *)buff) = 1; // no erase-block hint available
    return RES_OK;
  default:
    return RES_PARERR;
  }
}

static const ff_diskio_impl_t eos_partition_diskio_impl = {
    .init = eos_ff_disk_init,
    .status = eos_ff_disk_status,
    .read = eos_ff_disk_read,
    .write = eos_ff_disk_write,
    .ioctl = eos_ff_disk_ioctl,
};

bool driver_storage_partition_fat_mount(eos_dev_t *dev, const char *path) {
  BYTE pdrv;

  if (ff_diskio_get_drive(&pdrv) != ESP_OK) {
    EOS_LOGE("partition: no free FatFs drive slots (FF_VOLUMES exhausted)");
    return false;
  }

  s_pdrv_map[pdrv] = dev;
  ff_diskio_register(pdrv, &eos_partition_diskio_impl);

  char drv[3] = {(char)('0' + pdrv), ':', 0};

  esp_vfs_fat_conf_t conf = {
      .base_path = path,
      .fat_drive = drv,
      .max_files = 4,
  };

  FATFS *fs;
  if (esp_vfs_fat_register(&conf, &fs) != ESP_OK) {
    EOS_LOGE("partition: esp_vfs_fat_register failed for %s", path);
    goto fail;
  }

  FRESULT res = f_mount(fs, drv, 1 /* force mount now */);
  if (res != FR_OK) {
    EOS_LOGE("partition: f_mount(%s) failed, FRESULT=%d", drv, res);
    esp_vfs_fat_unregister_path(path);
    goto fail;
  }

  partition_state_t *state = dev->state;
  state->fat_mounted = true;
  state->fat_pdrv = pdrv;
  state->fat_fs = fs;
  strlcpy(state->mount_path, path, sizeof(state->mount_path));

  // Register in EOS
  eos_vfs_register_dummy(path);

  EOS_LOGI("partition: mounted %s at %s (pdrv=%d)", dev->name, path, pdrv);
  return true;

fail:
  ff_diskio_register(pdrv, NULL);
  s_pdrv_map[pdrv] = NULL;
  return false;
}

bool driver_storage_partition_fat_umount(eos_dev_t *dev) {
  partition_state_t *state = dev->state;
  if (!state || !state->fat_mounted)
    return false;

  char drv[3] = {(char)('0' + state->fat_pdrv), ':', 0};

  // Unmount FatFs first (fs=NULL, opt=0 just detaches the volume) - do
  // this before tearing down the VFS registration/diskio glue, or FatFs
  // could still try to touch a pdrv that's no longer backed by anything.
  FRESULT res = f_mount(NULL, drv, 0);
  if (res != FR_OK) {
    EOS_LOGE("partition: f_mount(NULL, %s) failed, FRESULT=%d", drv, res);
    return false;
  }

  ff_diskio_register(state->fat_pdrv, NULL);
  s_pdrv_map[state->fat_pdrv] = NULL;

  // Unregister in EOS
  esp_vfs_fat_unregister_path(state->mount_path);

  EOS_LOGI("partition: unmounted %s from %s (pdrv=%d)", dev->name,
           state->mount_path, state->fat_pdrv);

  eos_vfs_unregister_dummy(state->mount_path);

  state->fat_mounted = false;
  state->fat_pdrv = 0;
  state->fat_fs = NULL;
  state->mount_path[0] = '\0';

  return true;
}

bool driver_storage_partition_umount(eos_dev_t *dev) {
  partition_state_t *state = dev->state;
  if (!state)
    return false;

  // Currently the only mount type is FatFs; this is the dispatch point
  // to extend if/when other filesystem types are supported.
  if (state->fat_mounted){
    return driver_storage_partition_fat_umount(dev);
  }

  EOS_LOGW("partition: %s is not mounted", dev->name);

  return false;
}

bool driver_storage_partition_mount(eos_dev_t *dev, const char *path) {
  EOS_LOGI("Trying to mount %s to %s\n", dev->name, path);
  partition_state_t *state = dev->state;

  if (eos_diskpart_is_fat(state->info->part_type))
    return driver_storage_partition_fat_mount(dev, path);

  return false;
}

int driver_storage_partition_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  partition_state_t *state = dev->state;

  switch (cmd) {
  case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE: {
    uint32_t *out = va_arg(args, uint32_t *);
    *out = state->sector_size;
    break;
  }
  case EOS_STORAGE_IOCTL_GET_CAPACITY: {
    uint32_t *out = va_arg(args, uint32_t *);
    *out = state->info->lba_size;
    break;
  }
  case EOS_STORAGE_IOCTL_MOUNT: {
    const char *path = va_arg(args, const char *);
    bool *result = va_arg(args, bool *);
    *result = driver_storage_partition_mount(dev, path);
    break;
  }
  case EOS_STORAGE_IOCTL_UMOUNT: {
    bool *result = va_arg(args, bool *);
    *result = driver_storage_partition_umount(dev);
    break;
  }
  }
  return EOS_ERR_NO_ERROR;
}

EOS_DRV_ATTR eos_drv_t driver_storage_partition = {
    EOS_DRV_INIT,
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
