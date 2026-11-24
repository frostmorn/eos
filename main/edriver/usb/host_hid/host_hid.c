#include "includes.h"
#ifdef EOS_DRV_USB_HOST_HID_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/psram_alloc.h"
#include "edriver/usb/host_hid/host_hid.h"

#include <errno.h>
#include <string.h>

bool driver_usb_host_hid_init(eos_dev_t *dev) {
  eos_usb_host_hid_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_host_hid_state_t));
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

  st->key_buf = eos_psram_malloc_prefer(EOS_USB_HOST_HID_BUFFER_SIZE);
  if (!st->key_buf) {
    vSemaphoreDelete(st->mutex);
    free(st);
    dev->state = NULL;
    return false;
  }
  st->buf_size = EOS_USB_HOST_HID_BUFFER_SIZE;
  st->keyboard_attached = true;

  EOS_LOGI("usb_host_hid: host HID keyboard driver initialized");
  return true;
}

void driver_usb_host_hid_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_host_hid_state_t *st = (eos_usb_host_hid_state_t *)dev->state;
  if (st->key_buf) {
    free(st->key_buf);
    st->key_buf = NULL;
  }
  if (st->mutex) {
    vSemaphoreDelete(st->mutex);
    st->mutex = NULL;
  }
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb_host_hid: host HID driver shutdown complete");
}

int driver_usb_host_hid_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_usb_host_hid_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_usb_host_hid_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_host_hid_state_t *st = (eos_usb_host_hid_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  size_t bytes_read = 0;
  uint8_t *out = (uint8_t *)dst;
  while (bytes_read < size && st->count > 0) {
    out[bytes_read++] = st->key_buf[st->tail];
    st->tail = (st->tail + 1) % st->buf_size;
    st->count--;
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)bytes_read;
}

ssize_t driver_usb_host_hid_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_host_hid_state_t *st = (eos_usb_host_hid_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  const uint8_t *in = (const uint8_t *)data;
  size_t bytes_written = 0;
  while (bytes_written < size && st->count < st->buf_size) {
    st->key_buf[st->head] = in[bytes_written++];
    st->head = (st->head + 1) % st->buf_size;
    st->count++;
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)bytes_written;
}

EOS_DRV_ATTR eos_drv_t driver_usb_host_hid = {
    EOS_DRV_INIT,
    .scope = "usb",
    .name = "host_hid",
    .devname = "usb_host_hid",
    .init = driver_usb_host_hid_init,
    .shutdown = driver_usb_host_hid_shutdown,
    .open = driver_usb_host_hid_open,
    .close = driver_usb_host_hid_close,
    .read = driver_usb_host_hid_read,
    .write = driver_usb_host_hid_write,
};

#endif
