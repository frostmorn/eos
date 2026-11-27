#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Mass Storage Driver Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ecore/dev.h"
#include "ecore/usb.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "emisc/strlimits.h"
#define EOS_USB_MSC_SIM_SECTOR_COUNT    128
#define EOS_USB_MSC_MBR_SIG_OFFSET_0    510
#define EOS_USB_MSC_MBR_SIG_OFFSET_1    511
#define EOS_USB_MSC_MBR_SIG_BYTE_0      0x55
#define EOS_USB_MSC_MBR_SIG_BYTE_1      0xAA

// SCSI Command Block Wrapper (31 bytes)
typedef struct __attribute__((packed)) {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_transfer_length;
  uint8_t  flags;
  uint8_t  lun;
  uint8_t  cb_length;
  uint8_t  cb[16];
} eos_usb_msc_cbw_t;

// SCSI Command Status Wrapper (13 bytes)
typedef struct __attribute__((packed)) {
  uint32_t signature;
  uint32_t tag;
  uint32_t data_residue;
  uint8_t  status;
} eos_usb_msc_csw_t;

typedef struct {
  SemaphoreHandle_t io_mutex;
  char     backing_dev_path[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
  eos_dev_t *backing_dev;
  uint32_t sector_count;
  uint32_t sector_size;
  bool     read_only;
  uint8_t *sector_buf;
  uint8_t *sim_storage;
  size_t   sim_storage_size;
  uint32_t last_scsi_cmd;
  uint32_t blocks_read;
  uint32_t blocks_written;
} eos_usb_msc_state_t;
