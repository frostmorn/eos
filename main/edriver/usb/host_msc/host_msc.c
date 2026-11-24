#include "includes.h"
#ifdef EOS_DRV_USB_HOST_MSC_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/psram_alloc.h"
#include "edriver/usb/host_msc/host_msc.h"

#include <errno.h>
#include <string.h>

bool driver_usb_host_msc_init(eos_dev_t *dev) {
  eos_usb_host_msc_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_host_msc_state_t));
  if (!st) {
    return false;
  }
  memset(st, 0, sizeof(*st));
  dev->state = st;

  st->mutex = xSemaphoreCreateMutex();
  if (!st->mutex) {
    free(st);
    dev->state = NULL;
    return false;
  }

  st->sector_size = EOS_USB_HOST_MSC_SECTOR_SIZE;
  st->sector_count = EOS_USB_HOST_MSC_DEFAULT_COUNT;
  st->storage_size = st->sector_size * st->sector_count;
  st->storage = eos_psram_malloc_prefer(st->storage_size);
  if (!st->storage) {
    vSemaphoreDelete(st->mutex);
    free(st);
    dev->state = NULL;
    return false;
  }
  memset(st->storage, 0, st->storage_size);
  st->storage[EOS_USB_HOST_MSC_MBR_SIG_OFF_0] = EOS_USB_HOST_MSC_MBR_SIG_VAL_0;
  st->storage[EOS_USB_HOST_MSC_MBR_SIG_OFF_1] = EOS_USB_HOST_MSC_MBR_SIG_VAL_1;
  st->device_attached = true;

  EOS_LOGI("usb_host_msc: host mass storage attached (sectors=%lu)", (unsigned long)st->sector_count);
  return true;
}

void driver_usb_host_msc_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_host_msc_state_t *st = (eos_usb_host_msc_state_t *)dev->state;
  if (st->storage) {
    free(st->storage);
    st->storage = NULL;
  }
  if (st->mutex) {
    vSemaphoreDelete(st->mutex);
    st->mutex = NULL;
  }
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb_host_msc: driver shutdown complete");
}

int driver_usb_host_msc_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_usb_host_msc_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_usb_host_msc_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_host_msc_state_t *st = (eos_usb_host_msc_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  if ((size_t)st->cur_offset >= st->storage_size) {
    xSemaphoreGive(st->mutex);
    return 0;
  }

  size_t avail = st->storage_size - (size_t)st->cur_offset;
  size_t to_read = (size < avail) ? size : avail;
  memcpy(dst, st->storage + st->cur_offset, to_read);
  st->cur_offset += to_read;

  xSemaphoreGive(st->mutex);
  return (ssize_t)to_read;
}

ssize_t driver_usb_host_msc_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_host_msc_state_t *st = (eos_usb_host_msc_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  if ((size_t)st->cur_offset >= st->storage_size) {
    xSemaphoreGive(st->mutex);
    errno = ENOSPC;
    return -1;
  }

  size_t avail = st->storage_size - (size_t)st->cur_offset;
  size_t to_write = (size < avail) ? size : avail;
  memcpy(st->storage + st->cur_offset, data, to_write);
  st->cur_offset += to_write;

  xSemaphoreGive(st->mutex);
  return (ssize_t)to_write;
}

off_t driver_usb_host_msc_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_host_msc_state_t *st = (eos_usb_host_msc_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  off_t new_pos = st->cur_offset;
  switch (whence) {
    case SEEK_SET:
      new_pos = offset;
      break;
    case SEEK_CUR:
      new_pos += offset;
      break;
    case SEEK_END:
      new_pos = (off_t)st->storage_size + offset;
      break;
    default:
      xSemaphoreGive(st->mutex);
      errno = EINVAL;
      return -1;
  }

  if (new_pos < 0 || (size_t)new_pos > st->storage_size) {
    xSemaphoreGive(st->mutex);
    errno = EINVAL;
    return -1;
  }

  st->cur_offset = new_pos;
  xSemaphoreGive(st->mutex);
  return new_pos;
}

int driver_usb_host_msc_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_host_msc_state_t *st = (eos_usb_host_msc_state_t *)dev->state;

  if (cmd == EOS_STORAGE_IOCTL_GET_SECTOR_SIZE) {
    uint32_t *sz_ptr = va_arg(args, uint32_t *);
    if (!sz_ptr) {
      errno = EINVAL;
      return -1;
    }
    *sz_ptr = st->sector_size;
    return 0;
  }
  if (cmd == EOS_STORAGE_IOCTL_GET_CAPACITY) {
    uint32_t *cnt_ptr = va_arg(args, uint32_t *);
    if (!cnt_ptr) {
      errno = EINVAL;
      return -1;
    }
    *cnt_ptr = st->sector_count * st->sector_size;
    return 0;
  }

  errno = ENOSYS;
  return -1;
}

EOS_DRV_ATTR eos_drv_t driver_usb_host_msc = {
    EOS_DRV_INIT,
    .scope = "usb",
    .name = "host_msc",
    .devname = "usb_host_msc",
    .init = driver_usb_host_msc_init,
    .shutdown = driver_usb_host_msc_shutdown,
    .open = driver_usb_host_msc_open,
    .close = driver_usb_host_msc_close,
    .read = driver_usb_host_msc_read,
    .write = driver_usb_host_msc_write,
    .lseek = driver_usb_host_msc_lseek,
    .ioctl = driver_usb_host_msc_ioctl,
};

#endif
