#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Virtual Modem Driver Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ecore/usb.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define EOS_USB_MODEM_BUFFER_SIZE       2048
#define EOS_USB_MODEM_CMD_MAX_LEN       128
#define EOS_USB_MODEM_DEFAULT_BAUD      115200
#define EOS_USB_MODEM_INFO_STR          "EOS USB CDC-ACM Virtual Modem v1.0\r\n"
#define EOS_USB_MODEM_MANUFACTURER_STR  "EOS-OS\r\n"
#define EOS_USB_MODEM_MODEL_STR         "EOS-VMODEM\r\n"
#define EOS_USB_MODEM_VERSION_STR       "1.0.0\r\n"

typedef enum {
  EOS_USB_MODEM_MODE_COMMAND = 0,
  EOS_USB_MODEM_MODE_DATA    = 1,
} eos_usb_modem_mode_t;

typedef struct {
  SemaphoreHandle_t mutex;
  eos_usb_modem_mode_t mode;
  eos_usb_modem_line_coding_t line_coding;
  bool dtr;
  bool rts;
  uint8_t *rx_buf;
  size_t   rx_size;
  size_t   rx_head;
  size_t   rx_tail;
  size_t   rx_count;
  uint8_t *tx_buf;
  size_t   tx_size;
  size_t   tx_head;
  size_t   tx_tail;
  size_t   tx_count;
  char     cmd_accum[EOS_USB_MODEM_CMD_MAX_LEN];
  size_t   cmd_len;
  uint32_t escape_plus_count;
} eos_usb_modem_state_t;
