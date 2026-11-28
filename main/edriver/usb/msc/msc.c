#include "includes.h"
#ifdef EOS_DRV_USB_MSC_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/fs.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/usb.h"
#include "emisc/psram_alloc.h"
#include "edriver/usb/msc/msc.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// ── Private Helpers ──────────────────────────────────────────

static void msc_open_backing_locked(eos_usb_msc_state_t *st);

#ifdef CONFIG_CHERRYUSB_DEVICE
void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num, uint32_t *block_size) {
  (void)busid;
  (void)lun;
  eos_dev_t *dev = eos_dev_find_by_name("usb_msc0");
  if (!dev || !dev->state) {
    *block_num = 0;
    *block_size = EOS_SECTOR_SIZE;
    return;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  xSemaphoreTake(st->io_mutex, portMAX_DELAY);
  if (!st->backing_dev && st->backing_dev_path[0] != '\0') {
    msc_open_backing_locked(st);
  }
  if (st->backing_dev && st->sector_count > 0 && st->sector_size > 0) {
    *block_num = st->sector_count;
    *block_size = st->sector_size;
  } else {
    *block_num = 0;
    *block_size = EOS_SECTOR_SIZE;
  }
  xSemaphoreGive(st->io_mutex);
}

int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length) {
  (void)busid;
  (void)lun;
  eos_dev_t *dev = eos_dev_find_by_name("usb_msc0");
  if (!dev || !dev->state || !buffer || length == 0) {
    return -1;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  xSemaphoreTake(st->io_mutex, portMAX_DELAY);
  if (!st->backing_dev && st->backing_dev_path[0] != '\0') {
    msc_open_backing_locked(st);
  }
  if (!st->backing_dev || st->sector_size == 0 || st->sector_count == 0) {
    xSemaphoreGive(st->io_mutex);
    return -1;
  }
  if (eos_dev_lseek(st->backing_dev, EOS_DRV_FD, (off_t)sector, SEEK_SET) < 0) {
    xSemaphoreGive(st->io_mutex);
    return -1;
  }
  ssize_t n = eos_dev_read(st->backing_dev, EOS_DRV_FD, buffer, length);
  if (n == (ssize_t)length) {
    st->blocks_read += length / st->sector_size;
    xSemaphoreGive(st->io_mutex);
    return 0;
  }
  xSemaphoreGive(st->io_mutex);
  return -1;
}

int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length) {
  (void)busid;
  (void)lun;
  eos_dev_t *dev = eos_dev_find_by_name("usb_msc0");
  if (!dev || !dev->state || !buffer || length == 0) {
    return -1;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  xSemaphoreTake(st->io_mutex, portMAX_DELAY);
  if (!st->backing_dev && st->backing_dev_path[0] != '\0') {
    msc_open_backing_locked(st);
  }
  if (!st->backing_dev || st->sector_size == 0 || st->sector_count == 0 || st->read_only) {
    xSemaphoreGive(st->io_mutex);
    return -1;
  }
  if (eos_dev_lseek(st->backing_dev, EOS_DRV_FD, (off_t)sector, SEEK_SET) < 0) {
    xSemaphoreGive(st->io_mutex);
    return -1;
  }
  ssize_t n = eos_dev_write(st->backing_dev, EOS_DRV_FD, buffer, length);
  if (n == (ssize_t)length) {
    st->blocks_written += length / st->sector_size;
    xSemaphoreGive(st->io_mutex);
    return 0;
  }
  xSemaphoreGive(st->io_mutex);
  return -1;
}
#endif

static void msc_open_backing_locked(eos_usb_msc_state_t *st) {
  if (!st) return;
  st->backing_dev = NULL;
  st->sector_count = 0;
  st->sector_size = EOS_SECTOR_SIZE;
  st->read_only = false;
  if (st->backing_dev_path[0] == '\0') {
    return;
  }

  const char *dev_name = st->backing_dev_path;
  if (strncmp(dev_name, "/dev/", 5) == 0) {
    dev_name += 5;
  }
  st->backing_dev = eos_dev_find_by_name(dev_name);

  if (st->backing_dev) {
    uint32_t sec_sz = 0;
    uint32_t cap = 0;
    if (eos_dev_ioctl_call(st->backing_dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_GET_SECTOR_SIZE, &sec_sz) == 0 && sec_sz > 0) {
      st->sector_size = sec_sz;
    }
    if (eos_dev_ioctl_call(st->backing_dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_GET_CAPACITY, &cap) == 0 && cap > 0) {
      st->sector_count = cap;
    } else {
      off_t end = eos_dev_lseek(st->backing_dev, EOS_DRV_FD, 0, SEEK_END);
      if (end > 0) {
        st->sector_count = (uint32_t)end;
        eos_dev_lseek(st->backing_dev, EOS_DRV_FD, 0, SEEK_SET);
      }
    }
    EOS_LOGI("usb_msc: Bound to backing device '%s' (sector_size=%lu, sectors=%lu, ro=%d)",
             st->backing_dev_path, (unsigned long)st->sector_size,
             (unsigned long)st->sector_count, (int)st->read_only);
    uint32_t usb_funcs = 0;
    eos_dev_t *msc_dev = eos_dev_find_by_name("usb_msc0");
    if (msc_dev && msc_dev->parent) {
      (void)eos_dev_ioctl(msc_dev->parent, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_GET_FUNCS, &usb_funcs);
    }
    if (usb_funcs & EOS_USB_GADGET_FUNC_MSC) {
      // Unmount any active local VFS mounts/partitions on this device for exclusive USB MSC access
      (void)eos_umount(st->backing_dev_path, 0);
      for (int p = 0; p < 4; p++) {
        char part_path[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
        snprintf(part_path, sizeof(part_path), "/dev/part%d", p);
        (void)eos_umount(part_path, 0);
        char part_name[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
        snprintf(part_name, sizeof(part_name), "part%d", p);
        eos_dev_t *pdev = eos_dev_find_by_name(part_name);
        if (pdev) {
          bool umount_ok = false;
          (void)eos_dev_ioctl_call(pdev, EOS_DRV_FD, EOS_STORAGE_IOCTL_UMOUNT, &umount_ok);
        }
      }
    }
    if (st->sector_count == 0 || st->sector_size == 0) {
      EOS_LOGW("usb_msc: '%s' has zero capacity; MSC unit not ready", st->backing_dev_path);
    }
  } else {
    EOS_LOGW("usb_msc: Backing device '%s' not found", st->backing_dev_path);
  }
}

static void msc_open_backing(eos_usb_msc_state_t *st) {
  if (!st) return;
  xSemaphoreTake(st->io_mutex, portMAX_DELAY);
  msc_open_backing_locked(st);
  xSemaphoreGive(st->io_mutex);
}

static void msc_init_sim_storage(eos_usb_msc_state_t *st) {
  st->sim_storage_size = EOS_USB_MSC_SIM_SECTOR_COUNT * EOS_SECTOR_SIZE;
  st->sim_storage = eos_psram_malloc_prefer(st->sim_storage_size);
  if (st->sim_storage) {
    memset(st->sim_storage, 0, st->sim_storage_size);
    // Write a dummy MBR partition signature at end of sector 0
    st->sim_storage[EOS_USB_MSC_MBR_SIG_OFFSET_0] = EOS_USB_MSC_MBR_SIG_BYTE_0;
    st->sim_storage[EOS_USB_MSC_MBR_SIG_OFFSET_1] = EOS_USB_MSC_MBR_SIG_BYTE_1;
  }
}

// ── Dev Operations ───────────────────────────────────────────

bool driver_usb_msc_init(eos_dev_t *dev) {
  eos_usb_msc_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_msc_state_t));
  if (!st) {
    return false;
  }
  memset(st, 0, sizeof(*st));
  dev->state = st;

  st->io_mutex = xSemaphoreCreateMutex();
  if (!st->io_mutex) {
    free(st);
    dev->state = NULL;
    return false;
  }

  st->sector_size = EOS_SECTOR_SIZE;
  st->sector_count = EOS_USB_MSC_SIM_SECTOR_COUNT;
  st->sector_buf = eos_psram_malloc_prefer(st->sector_size);
  if (!st->sector_buf) {
    vSemaphoreDelete(st->io_mutex);
    free(st);
    dev->state = NULL;
    return false;
  }

  msc_init_sim_storage(st);

  const char *backing = eos_cfg_get_s(dev->cfg, "backing_dev", "/dev/sd0");
  strncpy(st->backing_dev_path, backing, sizeof(st->backing_dev_path) - 1);
  st->backing_dev = NULL;
  msc_open_backing(st);

  EOS_LOGI("usb_msc: Mass Storage Class driver initialized (backing=%s, sectors=%lu)",
           st->backing_dev_path, (unsigned long)st->sector_count);
  return true;
}

void driver_usb_msc_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  if (st->io_mutex) {
    xSemaphoreTake(st->io_mutex, portMAX_DELAY);
    st->backing_dev = NULL;
    st->sector_count = 0;
    xSemaphoreGive(st->io_mutex);
  } else {
    st->backing_dev = NULL;
  }
  if (st->sim_storage) {
    free(st->sim_storage);
    st->sim_storage = NULL;
  }
  if (st->sector_buf) {
    free(st->sector_buf);
    st->sector_buf = NULL;
  }
  if (st->io_mutex) {
    vSemaphoreDelete(st->io_mutex);
    st->io_mutex = NULL;
  }
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb_msc: Mass Storage driver shutdown complete");
}

int driver_usb_msc_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_usb_msc_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_usb_msc_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  xSemaphoreTake(st->io_mutex, portMAX_DELAY);

  if (st->backing_dev_path[0] != '\0') {
    if (!st->backing_dev) {
      msc_open_backing_locked(st);
    }
    if (!st->backing_dev) {
      xSemaphoreGive(st->io_mutex);
      errno = ENODEV;
      return -1;
    }
    ssize_t ret = eos_dev_read(st->backing_dev, EOS_DRV_FD, dst, size);
    if (ret > 0) {
      st->blocks_read += (uint32_t)(ret / st->sector_size);
    }
    xSemaphoreGive(st->io_mutex);
    return ret;
  }

  size_t to_read = size;
  if (st->sim_storage && st->sim_storage_size > 0) {
    if (to_read > st->sim_storage_size) {
      to_read = st->sim_storage_size;
    }
    memcpy(dst, st->sim_storage, to_read);
    st->blocks_read += (uint32_t)(to_read / st->sector_size);
  } else {
    memset(dst, 0, to_read);
  }

  xSemaphoreGive(st->io_mutex);
  return (ssize_t)to_read;
}

ssize_t driver_usb_msc_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  xSemaphoreTake(st->io_mutex, portMAX_DELAY);

  if (st->backing_dev_path[0] != '\0') {
    if (!st->backing_dev) {
      msc_open_backing_locked(st);
    }
    if (!st->backing_dev) {
      xSemaphoreGive(st->io_mutex);
      errno = ENODEV;
      return -1;
    }
    if (st->read_only) {
      xSemaphoreGive(st->io_mutex);
      errno = EROFS;
      return -1;
    }
    ssize_t ret = eos_dev_write(st->backing_dev, EOS_DRV_FD, data, size);
    if (ret > 0) {
      st->blocks_written += (uint32_t)(ret / st->sector_size);
    }
    xSemaphoreGive(st->io_mutex);
    return ret;
  }

  // Virtual RAM storage only when no backing device is configured
  size_t to_write = size;
  if (st->sim_storage && st->sim_storage_size > 0) {
    if (to_write > st->sim_storage_size) {
      to_write = st->sim_storage_size;
    }
    memcpy(st->sim_storage, data, to_write);
    st->blocks_written += (uint32_t)(to_write / st->sector_size);
  }

  xSemaphoreGive(st->io_mutex);
  return (ssize_t)to_write;
}

off_t driver_usb_msc_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  xSemaphoreTake(st->io_mutex, portMAX_DELAY);
  off_t res = -1;
  if (st->backing_dev) {
    res = eos_dev_lseek(st->backing_dev, EOS_DRV_FD, offset, whence);
  } else {
    errno = ENODEV;
  }
  xSemaphoreGive(st->io_mutex);
  return res;
}

int driver_usb_msc_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;

  switch (cmd) {
    case EOS_USB_IOCTL_GADGET_SET_MSC_DEV: {
      const char *path = va_arg(args, const char *);
      if (!path) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->io_mutex, portMAX_DELAY);
      strncpy(st->backing_dev_path, path, sizeof(st->backing_dev_path) - 1);
      st->backing_dev_path[sizeof(st->backing_dev_path) - 1] = '\0';
      msc_open_backing_locked(st);
      xSemaphoreGive(st->io_mutex);
      return 0;
    }

    case EOS_USB_IOCTL_GADGET_GET_MSC_DEV: {
      char *out_path = va_arg(args, char *);
      if (!out_path) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->io_mutex, portMAX_DELAY);
      strncpy(out_path, st->backing_dev_path, EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1);
      out_path[EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1] = '\0';
      xSemaphoreGive(st->io_mutex);
      return 0;
    }

    case EOS_STORAGE_IOCTL_GET_SECTOR_SIZE: {
      uint32_t *out_sz = va_arg(args, uint32_t *);
      if (!out_sz) {
        errno = EINVAL;
        return -1;
      }
      *out_sz = st->sector_size;
      return 0;
    }

    case EOS_STORAGE_IOCTL_GET_CAPACITY: {
      uint32_t *out_cnt = va_arg(args, uint32_t *);
      if (!out_cnt) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->io_mutex, portMAX_DELAY);
      if (!st->backing_dev && st->backing_dev_path[0] != '\0') {
        msc_open_backing_locked(st);
      }
      *out_cnt = st->sector_count;
      xSemaphoreGive(st->io_mutex);
      return 0;
    }

    default:
      errno = ENOSYS;
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_usb_msc = {
    EOS_DRV_INIT,
    .scope = "usb",
    .name = "msc",
    .devname = "usb_msc",
    .init = driver_usb_msc_init,
    .shutdown = driver_usb_msc_shutdown,
    .open = driver_usb_msc_open,
    .close = driver_usb_msc_close,
    .read = driver_usb_msc_read,
    .write = driver_usb_msc_write,
    .lseek = driver_usb_msc_lseek,
    .ioctl = driver_usb_msc_ioctl,
};

#endif
