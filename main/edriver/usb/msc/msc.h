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

#define EOS_USB_MSC_SECTOR_SIZE         512
#define EOS_USB_MSC_SIM_SECTOR_COUNT    128
#define EOS_USB_MSC_MBR_SIG_OFFSET_0    510
#define EOS_USB_MSC_MBR_SIG_OFFSET_1    511
#define EOS_USB_MSC_MBR_SIG_BYTE_0      0x55
#define EOS_USB_MSC_MBR_SIG_BYTE_1      0xAA
#define EOS_USB_MSC_SCSI_RMB_FLAG       0x80
#define EOS_USB_MSC_SCSI_ANSI_2         0x02
#define EOS_USB_MSC_SCSI_RESP_FMT_2     0x02
#define EOS_USB_MSC_SCSI_INQ_ADD_LEN    31
#define EOS_USB_MSC_SCSI_INQ_LEN        36
#define EOS_USB_MSC_CBW_SIGNATURE       0x43425355
#define EOS_USB_MSC_CSW_SIGNATURE       0x53425355
#define EOS_USB_MSC_SCSI_INQUIRY        0x12
#define EOS_USB_MSC_SCSI_READ_CAP_10    0x25
#define EOS_USB_MSC_SCSI_READ_10        0x28
#define EOS_USB_MSC_SCSI_WRITE_10       0x2A
#define EOS_USB_MSC_SCSI_TEST_UNIT      0x00
#define EOS_USB_MSC_SCSI_REQ_SENSE      0x03
#define EOS_USB_MSC_SCSI_MODE_SENSE_6   0x1A
#define EOS_USB_MSC_SCSI_PREVENT_ALLOW  0x1E
#define EOS_USB_MSC_SCSI_VERIFY_10      0x2F
#define EOS_USB_MSC_SCSI_SYNC_CACHE_10  0x35

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
