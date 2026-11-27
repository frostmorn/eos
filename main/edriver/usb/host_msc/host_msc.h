#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Host MSC Driver Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "emisc/strlimits.h"
#define EOS_USB_HOST_MSC_DEFAULT_COUNT  256
#define EOS_USB_HOST_MSC_MBR_SIG_OFF_0  510
#define EOS_USB_HOST_MSC_MBR_SIG_OFF_1  511
#define EOS_USB_HOST_MSC_MBR_SIG_VAL_0  0x55
#define EOS_USB_HOST_MSC_MBR_SIG_VAL_1  0xAA

typedef struct {
  SemaphoreHandle_t io_mutex;
  uint32_t sector_size;
  uint32_t sector_count;
  uint8_t *storage;
  size_t   storage_size;
  off_t    cur_offset;
  bool     device_attached;
} eos_usb_host_msc_state_t;
