#include "includes.h"
#ifdef EOS_DRV_USB_HOST_CDC_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "emisc/psram_alloc.h"
#include "edriver/usb/host_cdc/host_cdc.h"

#include <errno.h>
#include <string.h>

bool driver_usb_host_cdc_init(eos_dev_t *dev) {
  eos_usb_host_cdc_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_host_cdc_state_t));
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

  st->rx_buf = eos_psram_malloc_prefer(EOS_USB_HOST_CDC_BUFFER_SIZE);
  if (!st->rx_buf) {
    vSemaphoreDelete(st->mutex);
    free(st);
    dev->state = NULL;
    return false;
  }
  st->rx_size = EOS_USB_HOST_CDC_BUFFER_SIZE;
  st->connected = true;

  EOS_LOGI("usb_host_cdc: host CDC-ACM device connected");
  return true;
}

void driver_usb_host_cdc_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_host_cdc_state_t *st = (eos_usb_host_cdc_state_t *)dev->state;
  if (st->rx_buf) {
    free(st->rx_buf);
    st->rx_buf = NULL;
  }
  if (st->mutex) {
    vSemaphoreDelete(st->mutex);
    st->mutex = NULL;
  }
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb_host_cdc: host CDC driver shutdown complete");
}

int driver_usb_host_cdc_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_usb_host_cdc_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_usb_host_cdc_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst || size == 0) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_host_cdc_state_t *st = (eos_usb_host_cdc_state_t *)dev->state;
  if (xSemaphoreTake(st->mutex, portMAX_DELAY) != pdTRUE) {
    errno = EIO;
    return -1;
  }

  size_t count = 0;
  char *p = (char *)dst;
  while (count < size && st->rx_count > 0) {
    p[count++] = (char)st->rx_buf[st->rx_tail];
    st->rx_tail = (st->rx_tail + 1) % st->rx_size;
    st->rx_count--;
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)count;
}

ssize_t driver_usb_host_cdc_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_host_cdc_state_t *st = (eos_usb_host_cdc_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  const uint8_t *in = (const uint8_t *)data;
  size_t bytes_written = 0;
  while (bytes_written < size && st->rx_count < st->rx_size) {
    st->rx_buf[st->rx_head] = in[bytes_written++];
    st->rx_head = (st->rx_head + 1) % st->rx_size;
    st->rx_count++;
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)bytes_written;
}

EOS_DRV_ATTR eos_drv_t driver_usb_host_cdc = {
    EOS_DRV_INIT,
    .scope = "usb",
    .name = "host_cdc",
    .devname = "usb_host_modem",
    .init = driver_usb_host_cdc_init,
    .shutdown = driver_usb_host_cdc_shutdown,
    .open = driver_usb_host_cdc_open,
    .close = driver_usb_host_cdc_close,
    .read = driver_usb_host_cdc_read,
    .write = driver_usb_host_cdc_write,
};

#endif
