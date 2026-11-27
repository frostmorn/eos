#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB MTP (Media Transfer Protocol) Driver Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ecore/dev.h"
#include "ecore/proc.h"
#include "ecore/usb.h"
#include "emisc/kvec.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#ifdef CONFIG_CHERRYUSB_DEVICE
#include "usbd_core.h"
#include "class/mtp/usb_mtp.h"
#endif

#define EOS_USB_MTP_DEFAULT_STORAGE_NAME  "Unified Storage"
#define EOS_USB_MTP_STR_LEN               64
#define EOS_USB_MTP_STORAGE_ID            0x00010001
#define EOS_USB_MTP_BUF_SIZE              1024
#define EOS_USB_MTP_MAX_HANDLES           512
#define EOS_USB_MTP_TASK_STACK            12288
#define EOS_USB_MTP_TASK_PRIO             5
#define EOS_USB_MTP_TX_TIMEOUT_MS         2000
#define EOS_USB_MTP_CONTAINER_HDR_LEN     12
#define EOS_USB_MTP_MAX_PARAMS            5
#define EOS_USB_MTP_ROOT_HANDLE           0x00000000
#define EOS_USB_MTP_ALL_HANDLES           0xFFFFFFFF
#define EOS_USB_MTP_VENDOR_EXT_ID         6
#define EOS_USB_MTP_VENDOR_EXT_VER        100
#define EOS_USB_MTP_STORAGE_FIXED_RAM     0x0003
#define EOS_USB_MTP_FS_HIERARCHICAL       0x0002
#define EOS_USB_MTP_ASSOC_GENERIC_FOLDER  0x0001
#define EOS_USB_MTP_CLASS_REQ_CANCEL      0x64
#define EOS_USB_MTP_CLASS_REQ_RESET       0x66
#define EOS_USB_MTP_CLASS_REQ_GET_STATUS  0x67
#define EOS_USB_MTP_ASCII_MASK            0x7F
#define EOS_USB_MTP_DEFAULT_TOTAL_BYTES   (64ULL * 1024ULL * 1024ULL)
#define EOS_USB_MTP_DEFAULT_FREE_BYTES    (32ULL * 1024ULL * 1024ULL)
#define EOS_USB_MTP_DIR_MODE              0755
#define EOS_USB_MTP_FILE_MODE             0644
#define EOS_USB_MTP_SHUTDOWN_WAIT_MS      50
#define EOS_USB_MTP_DMA_ALIGN             64
#define EOS_USB_MTP_MAC_LEN               6
#if defined(CONFIG_EOS_BOARD_JC4880P443C_I_W_Y)
#define EOS_USB_MTP_BOARD_NAME            "JC4880P443C"
#elif defined(CONFIG_EOS_BOARD_LILKA_V23)
#define EOS_USB_MTP_BOARD_NAME            "Lilka"
#elif defined(CONFIG_EOS_BOARD_ESP32S3_QEMU)
#define EOS_USB_MTP_BOARD_NAME            "QEMU"
#else
#define EOS_USB_MTP_BOARD_NAME            "Device"
#endif

typedef struct {
  uint32_t handle;
  uint32_t parent_handle;
  uint16_t format;
  uint32_t size;
  bool     is_dir;
  bool     read_only;
  bool     in_use;
  char     path[EOS_USB_MTP_MAX_PATH_LEN];
} eos_usb_mtp_obj_t;

typedef struct {
  uint32_t length;
  uint16_t type;
  uint16_t code;
  uint32_t tx_id;
  uint32_t params[EOS_USB_MTP_MAX_PARAMS];
  uint8_t  param_count;
} eos_usb_mtp_cmd_t;

typedef struct {
  SemaphoreHandle_t  io_mutex;
  char               path_spec[EOS_USB_MTP_MAX_PATH_LEN];
  kvec_psram_t(eos_usb_mtp_obj_t) handles;
  uint32_t           next_handle;
  uint32_t           session_id;
  bool               session_open;
  uint32_t           pending_send_handle;
  uint32_t           pending_send_size;
  bool               has_pending_send;
  uint8_t            bus_id;
  uint8_t            out_ep;
  uint8_t            in_ep;
  uint8_t            int_ep;
  char               model_str[EOS_USB_MTP_STR_LEN];
  char               serial_str[EOS_USB_MTP_STR_LEN];
  bool               bus_attached;
  bool               configured;
  uint8_t           *rx_buf;
  uint8_t           *tx_buf;
  uint32_t           rx_nbytes;
  SemaphoreHandle_t  rx_sem;
  SemaphoreHandle_t  tx_sem;
  eos_ktask_t        worker_ktask;
  bool               task_running;
#ifdef CONFIG_CHERRYUSB_DEVICE
  struct usbd_interface intf;
  struct usbd_endpoint  ep_out_desc;
  struct usbd_endpoint  ep_in_desc;
  struct usbd_endpoint  ep_int_desc;
#endif
} eos_usb_mtp_state_t;
