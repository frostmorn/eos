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

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#if CONFIG_TINYUSB_CDC_ENABLED
#include <tinyusb.h>
#include <tinyusb_cdc_acm.h>
#endif
#endif

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

#if CONFIG_TINYUSB_CDC_ENABLED
  if (tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0) && st->rx_count == 0) {
    size_t rx_sz = 0;
    if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, (uint8_t *)dst, size, &rx_sz) == ESP_OK && rx_sz > 0) {
      st->rx_total += (uint32_t)rx_sz;
      xSemaphoreGive(st->mutex);
      return (ssize_t)rx_sz;
    }
    if (size > 0) {
      xSemaphoreGive(st->mutex);
      errno = EAGAIN;
      return -1;
    }
  }
#endif

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

#if CONFIG_TINYUSB_CDC_ENABLED
  if (tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0)) {
    size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, src, size);
    esp_err_t flush_err = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(10));
    if (queued == 0 && size > 0 && flush_err != ESP_OK) {
      xSemaphoreGive(st->mutex);
      errno = EIO;
      return -1;
    }
    st->tx_total += (uint32_t)queued;
    xSemaphoreGive(st->mutex);
    return (ssize_t)queued;
  }
#endif

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
