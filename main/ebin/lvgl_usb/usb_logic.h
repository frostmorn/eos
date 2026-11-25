#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ecore/usb.h"

#define EOS_USB_LOGIC_MAX_RESP        64
#define EOS_USB_LOGIC_LABEL_LEN       32
#define EOS_USB_LOGIC_DEF_DEV         "/dev/usb0"
#define EOS_USB_LOGIC_SERIAL_DEV      "/dev/usb_serial0"
#define EOS_USB_LOGIC_MODEM_DEV       "/dev/usb_modem0"
#define EOS_USB_LOGIC_MAX_MSC_DEVS    8
#define EOS_USB_LOGIC_SERIAL_POLL_US  10000
#define EOS_USB_LOGIC_SERIAL_POLL_TRIES 10

typedef struct {
  int fd;
  char dev_path[32];
  eos_usb_bus_status_t status;
  eos_usb_role_t active_role;
  uint32_t gadget_mask;
  char msc_backing[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
  bool serial_test_passed;
  char modem_test_response[EOS_USB_LOGIC_MAX_RESP];
  bool last_test_serial_valid;
  bool last_test_modem_valid;
} usb_logic_ctx_t;

int usb_logic_init(usb_logic_ctx_t *ctx);
int usb_logic_select_dev(usb_logic_ctx_t *ctx, const char *dev_path);
void usb_logic_close(usb_logic_ctx_t *ctx);
int usb_logic_refresh(usb_logic_ctx_t *ctx);
int usb_logic_set_role(usb_logic_ctx_t *ctx, eos_usb_role_t role);
int usb_logic_toggle_gadget(usb_logic_ctx_t *ctx, uint32_t func_bit, bool enable);
int usb_logic_set_msc_backing(usb_logic_ctx_t *ctx, const char *path);
int usb_logic_cycle_msc_device(usb_logic_ctx_t *ctx);
int usb_logic_run_serial_echo(usb_logic_ctx_t *ctx);
int usb_logic_run_modem_ping(usb_logic_ctx_t *ctx, const char *cmd);
void usb_logic_format_role(eos_usb_role_t role, char *buf, size_t buflen);
void usb_logic_format_speed(eos_usb_speed_t speed, char *buf, size_t buflen);
void usb_logic_format_status(const usb_logic_ctx_t *ctx, char *buf, size_t buflen);
