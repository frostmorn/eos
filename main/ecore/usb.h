#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Subsystem Core Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EOS_USB_MSC_MAX_BACKING_DEV_LEN   32
#define EOS_USB_DEFAULT_MSC_BACKING       "/dev/sd0"
#define EOS_USB_MTP_MAX_PATH_LEN          64
#define EOS_USB_DEFAULT_MTP_PATH          "/mnt/sd:/tmp:/proc"
#define EOS_USB_MTP_FALLBACK_TOTAL_BYTES  (1024ULL * 1024ULL * 1024ULL)
#define EOS_USB_MTP_FALLBACK_FREE_BYTES   (512ULL * 1024ULL * 1024ULL)
#define EOS_USB_MAX_HOST_DEVICES          8

// USB Roles
typedef enum {
  EOS_USB_ROLE_OFF              = 0,
  EOS_USB_ROLE_HOST             = 1,
  EOS_USB_ROLE_DEVICE_COMPOSITE = 2,
  EOS_USB_ROLE_DEBUG_USJ        = 3, // Hardware USB-Serial-JTAG (JTAG + CDC console)
} eos_usb_role_t;

// USB Bus Speeds
typedef enum {
  EOS_USB_SPEED_UNKNOWN = 0,
  EOS_USB_SPEED_LOW     = 1,
  EOS_USB_SPEED_FULL    = 2,
  EOS_USB_SPEED_HIGH    = 3,
} eos_usb_speed_t;

// Composite Gadget Function Bitmask Flags
typedef enum {
  EOS_USB_GADGET_FUNC_NONE   = 0,
  EOS_USB_GADGET_FUNC_SERIAL = (1 << 0), // CDC-ACM Serial (/dev/usb_serial0)
  EOS_USB_GADGET_FUNC_MODEM  = (1 << 1), // CDC-ACM Modem (/dev/usb_modem0)
  EOS_USB_GADGET_FUNC_MSC    = (1 << 2), // Mass Storage (/dev/usb_msc0) - Exclusive Block Access
  EOS_USB_GADGET_FUNC_MTP    = (1 << 3), // Media Transfer (/dev/usb_mtp0) - Shared Simultaneous VFS Access
  EOS_USB_GADGET_FUNC_ALL    = (EOS_USB_GADGET_FUNC_SERIAL | EOS_USB_GADGET_FUNC_MODEM | EOS_USB_GADGET_FUNC_MSC | EOS_USB_GADGET_FUNC_MTP),
} eos_usb_gadget_func_t;

// Bus Status
typedef struct {
  uint8_t         port_id;
  eos_usb_role_t  active_role;
  eos_usb_speed_t speed;
  bool            vbus_present;
  bool            host_device_connected;
  uint32_t        gadget_funcs_enabled;
  char            msc_backing_dev[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
  char            mtp_base_path[EOS_USB_MTP_MAX_PATH_LEN];
  uint32_t        rx_bytes;
  uint32_t        tx_bytes;
} eos_usb_bus_status_t;

// Composite Gadget Configuration
typedef struct {
  uint32_t funcs;
  char     msc_backing_dev[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
  char     mtp_base_path[EOS_USB_MTP_MAX_PATH_LEN];
  uint16_t vid;
  uint16_t pid;
  char     manufacturer[32];
  char     product[32];
} eos_usb_gadget_cfg_t;

// Downstream Host Device Info
typedef struct {
  uint8_t         dev_addr;
  uint16_t        vid;
  uint16_t        pid;
  eos_usb_speed_t speed;
  char            class_name[16];
} eos_usb_host_dev_info_t;

// Downstream Host Device List
typedef struct {
  uint32_t                count;
  eos_usb_host_dev_info_t devices[EOS_USB_MAX_HOST_DEVICES];
} eos_usb_host_dev_list_t;

// CDC Serial / Modem Line Coding
typedef struct {
  uint32_t baudrate;
  uint8_t  char_format;
  uint8_t  parity;
  uint8_t  data_bits;
} eos_usb_line_coding_t;

typedef eos_usb_line_coding_t eos_usb_modem_line_coding_t;

// Virtual JTAG Test Pattern (for backwards compatibility / diagnostic TAP)
typedef struct {
  uint32_t tms;
  uint32_t tdi;
  uint32_t tdo;
  uint32_t bits;
} eos_usb_jtag_pattern_t;
