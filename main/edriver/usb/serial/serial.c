#include "includes.h"
#ifdef EOS_DRV_USB_SERIAL_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/usb.h"
#include "emisc/psram_alloc.h"
#include "edriver/usb/serial/serial.h"

#include <errno.h>
#include <string.h>

bool driver_usb_serial_init(eos_dev_t *dev) {
  eos_usb_serial_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_serial_state_t));
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

  st->rx_buf = eos_psram_malloc_prefer(EOS_USB_SERIAL_RING_BUF_SIZE);
  st->tx_buf = eos_psram_malloc_prefer(EOS_USB_SERIAL_RING_BUF_SIZE);
  if (!st->rx_buf || !st->tx_buf) {
    if (st->rx_buf) free(st->rx_buf);
    if (st->tx_buf) free(st->tx_buf);
    vSemaphoreDelete(st->mutex);
    free(st);
    dev->state = NULL;
    return false;
  }

  st->coding.baudrate = EOS_USB_SERIAL_DEFAULT_BAUD;
  st->coding.data_bits = 8;
  st->coding.char_format = 0;
  st->coding.parity = 0;
  st->connected = true;

  EOS_LOGI("usb_serial: CDC-ACM serial communication channel initialized");
  return true;
}

void driver_usb_serial_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_serial_state_t *st = (eos_usb_serial_state_t *)dev->state;
  free(st->rx_buf);
  free(st->tx_buf);
  if (st->mutex) {
    vSemaphoreDelete(st->mutex);
  }
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb_serial: CDC-ACM serial channel shutdown");
}

int driver_usb_serial_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_usb_serial_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_usb_serial_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_serial_state_t *st = (eos_usb_serial_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  size_t to_copy = (size < st->rx_count) ? size : st->rx_count;
  for (size_t i = 0; i < to_copy; i++) {
    ((uint8_t *)dst)[i] = st->rx_buf[st->rx_tail];
    st->rx_tail = (st->rx_tail + 1) % EOS_USB_SERIAL_RING_BUF_SIZE;
  }
  st->rx_count -= to_copy;
  st->rx_total += (uint32_t)to_copy;

  xSemaphoreGive(st->mutex);
  return (ssize_t)to_copy;
}

ssize_t driver_usb_serial_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_serial_state_t *st = (eos_usb_serial_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  const uint8_t *src = (const uint8_t *)data;

  if (dev->parent) {
    size_t sent = 0;
    if (eos_dev_ioctl(dev->parent, EOS_DRV_FD, EOS_USB_IOCTL_CDC_SEND, (uint8_t)0, src, size, &sent) == 0 && sent > 0) {
      st->tx_total += (uint32_t)sent;
      xSemaphoreGive(st->mutex);
      return (ssize_t)sent;
    }
  }

  size_t written = 0;
  for (size_t i = 0; i < size; i++) {
    if (st->rx_count < EOS_USB_SERIAL_RING_BUF_SIZE) {
      // Loopback for local test streaming
      st->rx_buf[st->rx_head] = src[i];
      st->rx_head = (st->rx_head + 1) % EOS_USB_SERIAL_RING_BUF_SIZE;
      st->rx_count++;
      written++;
    }
  }
  st->tx_total += (uint32_t)written;

  xSemaphoreGive(st->mutex);
  return (ssize_t)written;
}

int driver_usb_serial_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_serial_state_t *st = (eos_usb_serial_state_t *)dev->state;

  switch (cmd) {
    case EOS_USB_IOCTL_GET_STATUS: {
      eos_usb_bus_status_t *out_st = va_arg(args, eos_usb_bus_status_t *);
      if (!out_st) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->mutex, portMAX_DELAY);
      out_st->rx_bytes = st->rx_total;
      out_st->tx_bytes = st->tx_total;
      out_st->host_device_connected = st->connected;
      xSemaphoreGive(st->mutex);
      return 0;
    }

    case EOS_USB_IOCTL_CDC_PUSH_RX: {
      const uint8_t *buf = va_arg(args, const uint8_t *);
      size_t len = va_arg(args, size_t);
      if (!buf) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->mutex, portMAX_DELAY);
      for (size_t i = 0; i < len; i++) {
        if (st->rx_count < EOS_USB_SERIAL_RING_BUF_SIZE) {
          st->rx_buf[st->rx_head] = buf[i];
          st->rx_head = (st->rx_head + 1) % EOS_USB_SERIAL_RING_BUF_SIZE;
          st->rx_count++;
        }
      }
      xSemaphoreGive(st->mutex);
      return 0;
    }

    default:
      errno = ENOSYS;
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_usb_serial = {
    EOS_DRV_INIT,
    .scope = "usb",
    .name = "serial",
    .devname = "usb_serial",
    .init = driver_usb_serial_init,
    .shutdown = driver_usb_serial_shutdown,
    .open = driver_usb_serial_open,
    .close = driver_usb_serial_close,
    .read = driver_usb_serial_read,
    .write = driver_usb_serial_write,
    .ioctl = driver_usb_serial_ioctl,
};

#endif
