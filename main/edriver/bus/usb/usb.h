#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Bus Driver Internal Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ecore/usb.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define EOS_USB_BUS_BUFFER_SIZE         2048
#define EOS_USB_BUS_TIMEOUT_MS          1000
#define EOS_USB_DEFAULT_PORT            0
#define EOS_USB_MAX_CHILDREN            8
#define EOS_USB_DEFAULT_VID             0x303A
#define EOS_USB_DEFAULT_PID             0x4001
#define EOS_USB_DESC_MANUFACTURER_STR   "EOS-OS"
#define EOS_USB_DESC_PRODUCT_STR        "EOS Dual-Mode USB Device"
#define EOS_USB_DESC_SERIAL_STR         "EOS00000001"
#define EOS_USB_DEFAULT_MSC_BACKING     "/dev/sd0"
#define EOS_USB_BOOTLOADER_BAUD         1200
#define EOS_USB_DEFAULT_GADGET_FUNCS    (EOS_USB_GADGET_FUNC_SERIAL | EOS_USB_GADGET_FUNC_MODEM | EOS_USB_GADGET_FUNC_MSC)

typedef struct {
  uint8_t           port_id;
  eos_usb_role_t    role;
  eos_usb_speed_t   speed;
  uint32_t          gadget_funcs_enabled;
  char              msc_backing_dev[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
  bool              vbus_present;
  bool              host_device_connected;
  uint32_t          rx_bytes;
  uint32_t          tx_bytes;
  eos_dev_t        *children[EOS_USB_MAX_CHILDREN];
  uint32_t          child_count;
  uint8_t          *bulk_buf;
  size_t            bulk_buf_len;
  SemaphoreHandle_t mutex;
  bool              hw_initialized;
  bool              tusb_initialized;
  void             *phy_handle;
  uint32_t          host_port_resets;
  eos_usb_host_dev_list_t host_devs;
} eos_usb_bus_state_t;
