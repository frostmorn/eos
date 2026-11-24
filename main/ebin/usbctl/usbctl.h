#pragma once
///////////////////////////////////////////////////////
// EOS Project - USB Control Utility Header
///////////////////////////////////////////////////////

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EOS_USBCTL_DEFAULT_DEV      "/dev/usb0"
#define EOS_USBCTL_SERIAL_DEV       "/dev/usb_serial0"
#define EOS_USBCTL_MODEM_DEV        "/dev/usb_modem0"
#define EOS_USBCTL_MSC_DEV          "/dev/usb_msc0"
#define EOS_USBCTL_PATH_BUF_LEN     64
#define EOS_USBCTL_LINE_BUF_LEN     128
#define EOS_USBCTL_DATA_BUF_LEN     256
#define EOS_USBCTL_SERIAL_TEST_MSG  "EOS_USB_PING"

typedef struct {
  const char *name;
  const char *help;
  int (*handler)(int argc, char **argv, int fd);
} eos_usbctl_subcmd_t;
