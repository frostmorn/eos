#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Host HID Driver Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define EOS_USB_HOST_HID_BUFFER_SIZE 256

typedef struct {
  SemaphoreHandle_t state_mutex;
  uint8_t *key_buf;
  size_t   buf_size;
  size_t   head;
  size_t   tail;
  size_t   count;
  bool     keyboard_attached;
} eos_usb_host_hid_state_t;
