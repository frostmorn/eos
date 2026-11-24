#include "includes.h"
#ifdef EOS_DRV_USB_MODEM_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/usb.h"
#include "emisc/psram_alloc.h"
#include "edriver/usb/modem/modem.h"

#include <errno.h>
#include <string.h>

// ── Private Helpers ──────────────────────────────────────────

static void modem_queue_rx(eos_usb_modem_state_t *st, const char *str) {
  size_t len = strlen(str);
  for (size_t i = 0; i < len; i++) {
    if (st->rx_count < st->rx_size) {
      st->rx_buf[st->rx_head] = (uint8_t)str[i];
      st->rx_head = (st->rx_head + 1) % st->rx_size;
      st->rx_count++;
    }
  }
}

static void modem_handle_at_command(eos_usb_modem_state_t *st) {
  char *cmd = st->cmd_accum;
  // Trim trailing CR/LF
  while (st->cmd_len > 0 && (cmd[st->cmd_len - 1] == '\r' || cmd[st->cmd_len - 1] == '\n')) {
    cmd[--st->cmd_len] = '\0';
  }

  if (st->cmd_len == 0) {
    return;
  }

  EOS_LOGI("usb_modem: AT cmd received: '%s'", cmd);

  if (strcasecmp(cmd, "AT") == 0) {
    modem_queue_rx(st, "OK\r\n");
  } else if (strcasecmp(cmd, "ATI") == 0) {
    modem_queue_rx(st, EOS_USB_MODEM_INFO_STR);
    modem_queue_rx(st, "OK\r\n");
  } else if (strcasecmp(cmd, "AT+CGMI") == 0) {
    modem_queue_rx(st, EOS_USB_MODEM_MANUFACTURER_STR);
    modem_queue_rx(st, "OK\r\n");
  } else if (strcasecmp(cmd, "AT+CGMM") == 0) {
    modem_queue_rx(st, EOS_USB_MODEM_MODEL_STR);
    modem_queue_rx(st, "OK\r\n");
  } else if (strcasecmp(cmd, "AT+CGMR") == 0) {
    modem_queue_rx(st, EOS_USB_MODEM_VERSION_STR);
    modem_queue_rx(st, "OK\r\n");
  } else if (strcasecmp(cmd, "AT+CSQ") == 0) {
    modem_queue_rx(st, "+CSQ: 31,99\r\nOK\r\n");
  } else if (strcasecmp(cmd, "AT+CGATT?") == 0) {
    modem_queue_rx(st, "+CGATT: 1\r\nOK\r\n");
  } else if (strcasecmp(cmd, "AT+COPS?") == 0) {
    modem_queue_rx(st, "+COPS: 0,0,\"EOS NET\",7\r\nOK\r\n");
  } else if (strncasecmp(cmd, "ATD", 3) == 0) {
    st->mode = EOS_USB_MODEM_MODE_DATA;
    modem_queue_rx(st, "CONNECT 115200\r\n");
    EOS_LOGI("usb_modem: call connected, entered DATA mode");
  } else if (strcasecmp(cmd, "ATH") == 0) {
    st->mode = EOS_USB_MODEM_MODE_COMMAND;
    modem_queue_rx(st, "NO CARRIER\r\nOK\r\n");
    EOS_LOGI("usb_modem: call hung up, returned to COMMAND mode");
  } else {
    // Default response for other AT commands
    modem_queue_rx(st, "OK\r\n");
  }

  st->cmd_len = 0;
  memset(st->cmd_accum, 0, sizeof(st->cmd_accum));
}

// ── Dev Operations ───────────────────────────────────────────

bool driver_usb_modem_init(eos_dev_t *dev) {
  eos_usb_modem_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_modem_state_t));
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

  st->rx_buf = eos_psram_malloc_prefer(EOS_USB_MODEM_BUFFER_SIZE);
  st->tx_buf = eos_psram_malloc_prefer(EOS_USB_MODEM_BUFFER_SIZE);
  if (!st->rx_buf || !st->tx_buf) {
    if (st->rx_buf) free(st->rx_buf);
    if (st->tx_buf) free(st->tx_buf);
    vSemaphoreDelete(st->mutex);
    free(st);
    dev->state = NULL;
    return false;
  }

  st->rx_size = EOS_USB_MODEM_BUFFER_SIZE;
  st->tx_size = EOS_USB_MODEM_BUFFER_SIZE;
  st->mode = EOS_USB_MODEM_MODE_COMMAND;
  st->line_coding.baudrate = EOS_USB_MODEM_DEFAULT_BAUD;
  st->line_coding.char_format = 0;
  st->line_coding.parity = 0;
  st->line_coding.data_bits = 8;
  st->dtr = true;
  st->rts = true;

  EOS_LOGI("usb_modem: CDC-ACM virtual modem initialized (baud=%lu)", (unsigned long)st->line_coding.baudrate);
  return true;
}

void driver_usb_modem_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_modem_state_t *st = (eos_usb_modem_state_t *)dev->state;
  if (st->rx_buf) {
    free(st->rx_buf);
    st->rx_buf = NULL;
  }
  if (st->tx_buf) {
    free(st->tx_buf);
    st->tx_buf = NULL;
  }
  if (st->mutex) {
    vSemaphoreDelete(st->mutex);
    st->mutex = NULL;
  }
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb_modem: modem shutdown complete");
}

int driver_usb_modem_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_usb_modem_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_usb_modem_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_modem_state_t *st = (eos_usb_modem_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  size_t bytes_read = 0;
  uint8_t *out = (uint8_t *)dst;
  while (bytes_read < size && st->rx_count > 0) {
    out[bytes_read++] = st->rx_buf[st->rx_tail];
    st->rx_tail = (st->rx_tail + 1) % st->rx_size;
    st->rx_count--;
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)bytes_read;
}

ssize_t driver_usb_modem_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_modem_state_t *st = (eos_usb_modem_state_t *)dev->state;
  xSemaphoreTake(st->mutex, portMAX_DELAY);

  const uint8_t *in = (const uint8_t *)data;
  size_t bytes_written = 0;

  for (size_t i = 0; i < size; i++) {
    char ch = (char)in[i];
    if (st->mode == EOS_USB_MODEM_MODE_COMMAND) {
      if (ch == '\r' || ch == '\n') {
        if (st->cmd_len < EOS_USB_MODEM_CMD_MAX_LEN - 1) {
          st->cmd_accum[st->cmd_len++] = ch;
        }
        modem_handle_at_command(st);
      } else {
        if (st->cmd_len < EOS_USB_MODEM_CMD_MAX_LEN - 1) {
          st->cmd_accum[st->cmd_len++] = ch;
        }
      }
      bytes_written++;
    } else {
      // DATA mode: pass-through or check for escape sequence "+++"
      if (ch == '+') {
        st->escape_plus_count++;
        if (st->escape_plus_count == 3) {
          st->mode = EOS_USB_MODEM_MODE_COMMAND;
          st->escape_plus_count = 0;
          modem_queue_rx(st, "OK\r\n");
          EOS_LOGI("usb_modem: escape +++ detected, returned to COMMAND mode");
        }
      } else {
        st->escape_plus_count = 0;
      }

      // Loopback or echo in DATA mode
      if (st->rx_count < st->rx_size) {
        st->rx_buf[st->rx_head] = in[i];
        st->rx_head = (st->rx_head + 1) % st->rx_size;
        st->rx_count++;
      }
      bytes_written++;
    }
  }

  xSemaphoreGive(st->mutex);
  return (ssize_t)bytes_written;
}

int driver_usb_modem_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_modem_state_t *st = (eos_usb_modem_state_t *)dev->state;

  switch (cmd) {
    case EOS_USB_IOCTL_GET_STATUS: {
      eos_usb_bus_status_t *out_st = va_arg(args, eos_usb_bus_status_t *);
      if (!out_st) {
        errno = EINVAL;
        return -1;
      }
      out_st->rx_bytes = (uint32_t)st->rx_count;
      out_st->tx_bytes = (uint32_t)st->tx_count;
      return 0;
    }

    default:
      errno = ENOSYS;
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_usb_modem = {
    EOS_DRV_INIT,
    .scope = "usb",
    .name = "modem",
    .devname = "usb_modem",
    .init = driver_usb_modem_init,
    .shutdown = driver_usb_modem_shutdown,
    .open = driver_usb_modem_open,
    .close = driver_usb_modem_close,
    .read = driver_usb_modem_read,
    .write = driver_usb_modem_write,
    .ioctl = driver_usb_modem_ioctl,
};

#endif
