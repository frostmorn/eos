#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Host CDC Driver Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define EOS_USB_HOST_CDC_BUFFER_SIZE 1024

typedef struct {
  SemaphoreHandle_t mutex;
  uint8_t *rx_buf;
  size_t   rx_size;
  size_t   rx_head;
  size_t   rx_tail;
  size_t   rx_count;
  bool     connected;
} eos_usb_host_cdc_state_t;
