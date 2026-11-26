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
#include <freertos/task.h>
#include <freertos/queue.h>
#ifdef CONFIG_CHERRYUSB_DEVICE
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usbd_msc.h"
#include "class/mtp/usb_mtp.h"
#endif

#define EOS_USB_BUS_BUFFER_SIZE         2048
#define EOS_USB_BUS_TIMEOUT_MS          1000
#define EOS_USB_DEFAULT_PORT            0
#define EOS_USB_DISCONNECT_SETTLE_MS    50
#define EOS_USB_MAX_CHILDREN            8
#define EOS_USB_DEFAULT_VID             0x303A
#define EOS_USB_DEFAULT_PID             0x4001
#define EOS_USB_DESC_MANUFACTURER_STR   "EOS-OS"
#define EOS_USB_DESC_PRODUCT_STR        "EOS Dual-Mode USB Device"
#define EOS_USB_DESC_SERIAL_STR         "EOS00000001"
#define EOS_USB_DEFAULT_MSC_BACKING     "/dev/sd0"
#define EOS_USB_BOOTLOADER_BAUD         1200
#define EOS_USB_DEFAULT_GADGET_FUNCS    (EOS_USB_GADGET_FUNC_SERIAL | EOS_USB_GADGET_FUNC_MODEM | EOS_USB_GADGET_FUNC_MTP)

#define EOS_USB_EP_CDC0_IN              0x81
#define EOS_USB_EP_CDC0_OUT             0x01
#define EOS_USB_EP_CDC0_INT             0x82

#define EOS_USB_EP_CDC1_IN              0x83
#define EOS_USB_EP_CDC1_OUT             0x02
#define EOS_USB_EP_CDC1_INT             0x84

#define EOS_USB_EP_STORAGE_IN           0x85
#define EOS_USB_EP_STORAGE_OUT          0x03
#define EOS_USB_EP_MTP_INT              0x86

#define EOS_USB_EP_NOTIF_SIZE           8
#define EOS_USB_EP_DATA_FS_SIZE         64
#define EOS_USB_EP_DATA_HS_SIZE         512
#define EOS_USB_EP_MTP_EVT_SIZE         64
#define EOS_USB_MTP_EVT_INTERVAL        1

#define EOS_USB_STRID_LANG              0
#define EOS_USB_STRID_MFG               1
#define EOS_USB_STRID_PRODUCT           2
#define EOS_USB_STRID_SERIAL            3
#define EOS_USB_STRID_CDC               4
#define EOS_USB_STRID_MODEM             5
#define EOS_USB_STRID_MSC               6
#define EOS_USB_STRID_MTP               7

#define EOS_USB_MAX_CONFIG_DESC_SIZE    512
#define EOS_USB_CDC_BUFFER_SIZE         2048
#define EOS_USB_STRING_DESC_COUNT       8
#define EOS_USB_EP_QUALIFIER_SIZE       10
#define EOS_USB_BCD_VERSION             0x0200
#define EOS_USB_BCD_DEVICE              0x0100
#define EOS_USB_DEV_CLASS_MISC          0xEF
#define EOS_USB_DEV_SUBCLASS_COMMON     0x02
#define EOS_USB_DEV_PROTOCOL_IAD        0x01
#define EOS_USB_TX_TIMEOUT_MS           500
#define EOS_USB_CONFIG_HDR_LEN          9
#define EOS_USB_WCID_VENDOR_CODE        0x01
#define EOS_USB_WCID_STR_LEN            18
#define EOS_USB_WCID_DESC_LEN           40
#define EOS_USB_MTP_ITF_NUM             4
#define EOS_USB_DMA_ALIGN               64
#define EOS_USB_CDC_RX_QUEUE_LEN        16
#define EOS_USB_RX_TASK_STACK           4096
#define EOS_USB_RX_TASK_PRIO            6

#define EOS_USB_CONFIG_POWER_MA         100

typedef struct {
  uint8_t  cdc_index;
  uint16_t len;
  uint8_t  data[EOS_USB_EP_DATA_HS_SIZE];
} eos_usb_cdc_rx_msg_t;

typedef struct {
  uint8_t           port_id;
  uint8_t           bus_id;
  eos_usb_role_t    role;
  eos_usb_speed_t   speed;
  uint32_t          gadget_funcs_enabled;
  char              msc_backing_dev[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
  char              mtp_base_path[EOS_USB_MTP_MAX_PATH_LEN];
  bool              vbus_present;
  bool              host_device_connected;
  uint32_t          rx_bytes;
  uint32_t          tx_bytes;
  eos_dev_t        *children[EOS_USB_MAX_CHILDREN];
  uint32_t          child_count;
  uint8_t          *bulk_buf;
  size_t            bulk_buf_len;
  SemaphoreHandle_t bus_mutex;
  bool              hw_initialized;
  bool              cusb_initialized;
  bool              mtp_initialized;
  void             *phy_handle;
  uint32_t          host_port_resets;
  eos_usb_host_dev_list_t host_devs;
#ifdef CONFIG_CHERRYUSB_DEVICE
  struct usbd_interface intf_cdc0_ctrl;
  struct usbd_interface intf_cdc0_data;
  struct usbd_endpoint  ep_cdc0_out;
  struct usbd_endpoint  ep_cdc0_in;
  struct usbd_interface intf_cdc1_ctrl;
  struct usbd_interface intf_cdc1_data;
  struct usbd_endpoint  ep_cdc1_out;
  struct usbd_endpoint  ep_cdc1_in;
  struct usbd_interface intf_storage;
  uint8_t               config_desc[EOS_USB_MAX_CONFIG_DESC_SIZE];
  uint32_t              config_desc_len;
  uint8_t              *cdc0_rx_buf;
  uint8_t              *cdc0_tx_buf;
  uint8_t              *cdc1_rx_buf;
  uint8_t              *cdc1_tx_buf;
  SemaphoreHandle_t     cdc0_tx_sem;
  SemaphoreHandle_t     cdc1_tx_sem;
  QueueHandle_t         cdc_rx_queue;
  TaskHandle_t          cdc_rx_task;
  volatile bool         cdc0_tx_busy;
  volatile bool         cdc1_tx_busy;
  volatile bool         usb_configured;
#endif
} eos_usb_bus_state_t;
