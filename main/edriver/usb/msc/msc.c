#include "includes.h"
#ifdef EOS_DRV_USB_MSC_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
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

#if CONFIG_TINYUSB_MSC_ENABLED
#include <tinyusb_msc.h>
#endif

// ── Private Helpers ──────────────────────────────────────────

static void msc_open_backing(eos_usb_msc_state_t *st) {
  if (!st) return;
  if (st->backing_fd >= 0) {
    close(st->backing_fd);
    st->backing_fd = -1;
  }
  if (st->backing_dev_path[0] == '\0') {
    return;
  }

  st->backing_fd = open(st->backing_dev_path, O_RDWR);
  if (st->backing_fd < 0) {
    st->backing_fd = open(st->backing_dev_path, O_RDONLY);
    if (st->backing_fd >= 0) {
      st->read_only = true;
    }
  } else {
    st->read_only = false;
  }

  if (st->backing_fd >= 0) {
    uint32_t sec_sz = 0;
    uint32_t cap = 0;
    if (ioctl(st->backing_fd, EOS_STORAGE_IOCTL_GET_SECTOR_SIZE, &sec_sz) == 0 && sec_sz > 0) {
      st->sector_size = sec_sz;
    }
    if (ioctl(st->backing_fd, EOS_STORAGE_IOCTL_GET_CAPACITY, &cap) == 0 && cap > 0) {
      st->sector_count = cap / st->sector_size;
    } else {
      off_t end = lseek(st->backing_fd, 0, SEEK_END);
      if (end > 0) {
        st->sector_count = (uint32_t)(end / st->sector_size);
        lseek(st->backing_fd, 0, SEEK_SET);
      }
    }
    EOS_LOGI("usb_msc: Bound to backing device '%s' (fd=%d, sector_size=%lu, sectors=%lu, ro=%d)",
             st->backing_dev_path, st->backing_fd, (unsigned long)st->sector_size,
             (unsigned long)st->sector_count, (int)st->read_only);
#if CONFIG_TINYUSB_MSC_ENABLED && SOC_SDMMC_HOST_SUPPORTED
    if (st->tusb_storage) {
      tinyusb_msc_delete_storage((tinyusb_msc_storage_handle_t)st->tusb_storage);
      st->tusb_storage = NULL;
    }
    void *card_ptr = NULL;
    if (ioctl(st->backing_fd, EOS_STORAGE_IOCTL_GET_CARD, &card_ptr) == 0 && card_ptr) {
      tinyusb_msc_storage_config_t tusb_msc_cfg = {
          .medium = { .card = (sdmmc_card_t *)card_ptr },
          .fat_fs = {
              .base_path = NULL,
              .config = { .max_files = 2, .allocation_unit_size = 0 },
              .do_not_format = true,
              .format_flags = 0,
          },
          .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
      };
      tinyusb_msc_storage_handle_t hdl = NULL;
      if (tinyusb_msc_new_storage_sdmmc(&tusb_msc_cfg, &hdl) == ESP_OK) {
        st->tusb_storage = hdl;
        EOS_LOGI("usb_msc: TinyUSB MSC SDMMC storage registered");
      }
    }
#endif
  } else {
    EOS_LOGW("usb_msc: Backing device '%s' not accessible (errno=%d), fallback to virtual sim storage",
             st->backing_dev_path, errno);
  }
}

static void msc_init_sim_storage(eos_usb_msc_state_t *st) {
  st->sim_storage_size = EOS_USB_MSC_SIM_SECTOR_COUNT * EOS_USB_MSC_SECTOR_SIZE;
  st->sim_storage = eos_psram_malloc_prefer(st->sim_storage_size);
  if (st->sim_storage) {
    memset(st->sim_storage, 0, st->sim_storage_size);
    // Write a dummy MBR partition signature at end of sector 0
    st->sim_storage[EOS_USB_MSC_MBR_SIG_OFFSET_0] = EOS_USB_MSC_MBR_SIG_BYTE_0;
    st->sim_storage[EOS_USB_MSC_MBR_SIG_OFFSET_1] = EOS_USB_MSC_MBR_SIG_BYTE_1;
  }
}

static void msc_fill_inquiry(uint8_t *dst) {
  memset(dst, 0, EOS_USB_MSC_SCSI_INQ_LEN);
  dst[0] = EOS_USB_MSC_SCSI_TEST_UNIT;
  dst[1] = EOS_USB_MSC_SCSI_RMB_FLAG;
  dst[2] = EOS_USB_MSC_SCSI_ANSI_2;
  dst[3] = EOS_USB_MSC_SCSI_RESP_FMT_2;
  dst[4] = EOS_USB_MSC_SCSI_INQ_ADD_LEN;
  memcpy(&dst[8], "EOS-OS  ", 8);
  memcpy(&dst[16], "VIRTUAL MSC DISK", 16);
  memcpy(&dst[32], "1.00", 4);
}

// ── Dev Operations ───────────────────────────────────────────

bool driver_usb_msc_init(eos_dev_t *dev) {
  eos_usb_msc_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_msc_state_t));
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

  st->sector_size = EOS_USB_MSC_SECTOR_SIZE;
  st->sector_count = EOS_USB_MSC_SIM_SECTOR_COUNT;
  st->sector_buf = eos_psram_malloc_prefer(st->sector_size);
  if (!st->sector_buf) {
    vSemaphoreDelete(st->mutex);
    free(st);
    dev->state = NULL;
    return false;
  }

  msc_init_sim_storage(st);

  const char *backing = eos_cfg_get_s(dev->cfg, "backing_dev", "/dev/sd0");
  strncpy(st->backing_dev_path, backing, sizeof(st->backing_dev_path) - 1);
  st->backing_fd = -1;
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
#if CONFIG_TINYUSB_MSC_ENABLED
  if (st->tusb_storage) {
    tinyusb_msc_delete_storage((tinyusb_msc_storage_handle_t)st->tusb_storage);
    st->tusb_storage = NULL;
  }
#endif
  if (st->backing_fd >= 0) {
    close(st->backing_fd);
    st->backing_fd = -1;
  }
  if (st->sim_storage) {
    free(st->sim_storage);
    st->sim_storage = NULL;
  }
  if (st->sector_buf) {
    free(st->sector_buf);
    st->sector_buf = NULL;
  }
  if (st->mutex) {
    vSemaphoreDelete(st->mutex);
    st->mutex = NULL;
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
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  if (st->backing_fd < 0 && st->backing_dev_path[0] != '\0') {
    msc_open_backing(st);
  }

  if (st->backing_fd >= 0) {
    ssize_t ret = read(st->backing_fd, dst, size);
    if (ret > 0) {
      st->blocks_read += (ret / st->sector_size);
      xSemaphoreGive(st->mutex);
      return ret;
    }
  }

  size_t to_read = size;
  if (st->sim_storage && st->sim_storage_size > 0) {
    if (to_read > st->sim_storage_size) {
      to_read = st->sim_storage_size;
    }
    memcpy(dst, st->sim_storage, to_read);
    st->blocks_read += (to_read / st->sector_size);
  } else {
    memset(dst, 0, to_read);
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)to_read;
}

ssize_t driver_usb_msc_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_msc_state_t *st = (eos_usb_msc_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  // Parse SCSI Command Block Wrapper if data size matches CBW
  if (size == sizeof(eos_usb_msc_cbw_t)) {
    const eos_usb_msc_cbw_t *cbw = (const eos_usb_msc_cbw_t *)data;
    if (cbw->signature == EOS_USB_MSC_CBW_SIGNATURE) {
      st->last_scsi_cmd = cbw->cb[0];
      if (cbw->cb[0] == EOS_USB_MSC_SCSI_INQUIRY && st->sector_buf) {
        msc_fill_inquiry(st->sector_buf);
      }
      EOS_LOGI("usb_msc: SCSI opcode 0x%02lx received", (unsigned long)st->last_scsi_cmd);
      xSemaphoreGive(st->mutex);
      return (ssize_t)size;
    }
  }

  if (st->backing_fd < 0 && st->backing_dev_path[0] != '\0') {
    msc_open_backing(st);
  }

  if (st->backing_fd >= 0) {
    if (st->read_only) {
      xSemaphoreGive(st->mutex);
      errno = EROFS;
      return -1;
    }
    ssize_t ret = write(st->backing_fd, data, size);
    if (ret > 0) {
      st->blocks_written += (ret / st->sector_size);
      xSemaphoreGive(st->mutex);
      return ret;
    }
  }

  // Otherwise treat as direct sector block write to sim storage
  size_t to_write = size;
  if (st->sim_storage && st->sim_storage_size > 0) {
    if (to_write > st->sim_storage_size) {
      to_write = st->sim_storage_size;
    }
    memcpy(st->sim_storage, data, to_write);
    st->blocks_written += (to_write / st->sector_size);
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)to_write;
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
      xSemaphoreTake(st->mutex, portMAX_DELAY);
      strncpy(st->backing_dev_path, path, sizeof(st->backing_dev_path) - 1);
      st->backing_dev_path[sizeof(st->backing_dev_path) - 1] = '\0';
      msc_open_backing(st);
      xSemaphoreGive(st->mutex);
      if (dev->parent) {
        eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, path);
      }
      return 0;
    }

    case EOS_USB_IOCTL_GADGET_GET_MSC_DEV: {
      char *out_path = va_arg(args, char *);
      if (!out_path) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->mutex, portMAX_DELAY);
      strncpy(out_path, st->backing_dev_path, EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1);
      out_path[EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1] = '\0';
      xSemaphoreGive(st->mutex);
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
      xSemaphoreTake(st->mutex, portMAX_DELAY);
      if (st->backing_fd < 0 && st->backing_dev_path[0] != '\0') {
        msc_open_backing(st);
      }
      *out_cnt = st->sector_count * st->sector_size;
      xSemaphoreGive(st->mutex);
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
    .ioctl = driver_usb_msc_ioctl,
};

#endif
