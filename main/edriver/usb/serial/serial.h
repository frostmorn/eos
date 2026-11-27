#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB CDC Serial Driver Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ecore/usb.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define EOS_USB_SERIAL_RING_BUF_SIZE    1024
#define EOS_USB_SERIAL_DEFAULT_BAUD     115200

typedef struct {
  SemaphoreHandle_t io_mutex;
  uint8_t *rx_buf;
  size_t   rx_head;
  size_t   rx_tail;
  size_t   rx_count;
  uint8_t *tx_buf;
  size_t   tx_head;
  size_t   tx_tail;
  size_t   tx_count;
  eos_usb_line_coding_t coding;
  bool     connected;
  uint32_t rx_total;
  uint32_t tx_total;
} eos_usb_serial_state_t;
