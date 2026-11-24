#include "ebin/lvgl_usb/usb_logic.h"
#include "ecore/ioctl.h"
#include "ecore/usb.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

void usb_logic_format_role(eos_usb_role_t role, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (role == EOS_USB_ROLE_DEBUG_USJ) {
    snprintf(buf, buflen, "DEBUG (JTAG+CDC)");
  } else if (role == EOS_USB_ROLE_DEVICE_COMPOSITE) {
    snprintf(buf, buflen, "DEVICE (Gadgets)");
  } else if (role == EOS_USB_ROLE_HOST) {
    snprintf(buf, buflen, "HOST (Master)");
  } else if (role == EOS_USB_ROLE_OFF) {
    snprintf(buf, buflen, "OFF");
  } else {
    snprintf(buf, buflen, "UNKNOWN");
  }
}

void usb_logic_format_speed(eos_usb_speed_t speed, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (speed == EOS_USB_SPEED_HIGH) {
    snprintf(buf, buflen, "High Speed (480M)");
  } else if (speed == EOS_USB_SPEED_FULL) {
    snprintf(buf, buflen, "Full Speed (12M)");
  } else if (speed == EOS_USB_SPEED_LOW) {
    snprintf(buf, buflen, "Low Speed (1.5M)");
  } else {
    snprintf(buf, buflen, "Inactive");
  }
}

void usb_logic_format_status(const usb_logic_ctx_t *ctx, char *buf, size_t buflen) {
  if (!ctx || !buf || buflen == 0) return;
  char role_buf[EOS_USB_LOGIC_LABEL_LEN];
  char speed_buf[EOS_USB_LOGIC_LABEL_LEN];
  usb_logic_format_role(ctx->active_role, role_buf, sizeof(role_buf));
  usb_logic_format_speed(ctx->status.speed, speed_buf, sizeof(speed_buf));

  snprintf(buf, buflen, "[%s] Role: %s | Speed: %s | VBUS: %s",
           ctx->dev_path[0] ? ctx->dev_path : EOS_USB_LOGIC_DEF_DEV,
           role_buf, speed_buf, ctx->status.vbus_present ? "OK" : "NO");
}

int usb_logic_refresh(usb_logic_ctx_t *ctx) {
  if (!ctx || ctx->fd < 0) return -1;

  memset(&ctx->status, 0, sizeof(ctx->status));
  if (ioctl(ctx->fd, EOS_USB_IOCTL_GET_STATUS, &ctx->status) == 0) {
    ctx->active_role = ctx->status.active_role;
    ctx->gadget_mask = ctx->status.gadget_funcs_enabled;
    strncpy(ctx->msc_backing, ctx->status.msc_backing_dev, sizeof(ctx->msc_backing) - 1);
    ctx->msc_backing[sizeof(ctx->msc_backing) - 1] = '\0';
    return 0;
  }
  return -1;
}

int usb_logic_select_dev(usb_logic_ctx_t *ctx, const char *dev_path) {
  if (!ctx || !dev_path) return -1;
  if (ctx->fd >= 3) {
    close(ctx->fd);
    ctx->fd = -1;
  }
  strncpy(ctx->dev_path, dev_path, sizeof(ctx->dev_path) - 1);
  ctx->dev_path[sizeof(ctx->dev_path) - 1] = '\0';

  ctx->fd = open(ctx->dev_path, O_RDWR);
  if (ctx->fd < 0) {
    return -1;
  }
  return usb_logic_refresh(ctx);
}

int usb_logic_init(usb_logic_ctx_t *ctx) {
  if (!ctx) return -1;
  memset(ctx, 0, sizeof(*ctx));
  ctx->fd = -1;
  return usb_logic_select_dev(ctx, EOS_USB_LOGIC_DEF_DEV);
}

void usb_logic_close(usb_logic_ctx_t *ctx) {
  if (!ctx) return;
  if (ctx->fd >= 3) {
    close(ctx->fd);
    ctx->fd = -1;
  }
}

int usb_logic_set_role(usb_logic_ctx_t *ctx, eos_usb_role_t role) {
  if (!ctx || ctx->fd < 0) return -1;
  if (ioctl(ctx->fd, EOS_USB_IOCTL_SET_ROLE, role) < 0) {
    return -1;
  }
  return usb_logic_refresh(ctx);
}

int usb_logic_toggle_gadget(usb_logic_ctx_t *ctx, uint32_t func_bit, bool enable) {
  if (!ctx || ctx->fd < 0) return -1;
  uint32_t new_mask = ctx->gadget_mask;
  if (enable) {
    new_mask |= func_bit;
  } else {
    new_mask &= ~func_bit;
  }

  if (ioctl(ctx->fd, EOS_USB_IOCTL_GADGET_SET_FUNCS, new_mask) < 0) {
    return -1;
  }
  return usb_logic_refresh(ctx);
}

int usb_logic_set_msc_backing(usb_logic_ctx_t *ctx, const char *path) {
  if (!ctx || ctx->fd < 0 || !path) return -1;
  if (ioctl(ctx->fd, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, path) < 0) {
    return -1;
  }
  return usb_logic_refresh(ctx);
}

int usb_logic_cycle_msc_device(usb_logic_ctx_t *ctx) {
  if (!ctx) return -1;
  static const char *devices[EOS_USB_LOGIC_MSC_DEV_COUNT] = {
      "/dev/sd0",
      "/dev/flash0",
      "/dev/part0",
      "/tmp/virtual.img",
  };
  int cur_idx = -1;
  for (int i = 0; i < EOS_USB_LOGIC_MSC_DEV_COUNT; i++) {
    if (strcmp(ctx->msc_backing, devices[i]) == 0) {
      cur_idx = i;
      break;
    }
  }
  int next_idx = (cur_idx + 1) % EOS_USB_LOGIC_MSC_DEV_COUNT;
  return usb_logic_set_msc_backing(ctx, devices[next_idx]);
}

int usb_logic_run_serial_echo(usb_logic_ctx_t *ctx) {
  if (!ctx) return -1;
  int sfd = open(EOS_USB_LOGIC_SERIAL_DEV, O_RDWR | O_NONBLOCK);
  if (sfd < 0) {
    ctx->last_test_serial_valid = true;
    ctx->serial_test_passed = false;
    return -1;
  }

  const char msg[] = "PING\n";
  ssize_t nw = write(sfd, msg, strlen(msg));
  close(sfd);

  ctx->last_test_serial_valid = true;
  ctx->serial_test_passed = (nw >= 0);
  return (nw >= 0) ? 0 : -1;
}

int usb_logic_run_modem_ping(usb_logic_ctx_t *ctx, const char *cmd) {
  if (!ctx) return -1;
  int mfd = open(EOS_USB_LOGIC_MODEM_DEV, O_RDWR);
  if (mfd < 0) {
    ctx->last_test_modem_valid = true;
    snprintf(ctx->modem_test_response, sizeof(ctx->modem_test_response), "ERR: OPEN_FAIL");
    return -1;
  }

  const char *to_send = (cmd && cmd[0]) ? cmd : "ATI\r\n";
  char cmd_buf[EOS_USB_LOGIC_MAX_RESP];
  snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", to_send);
  write(mfd, cmd_buf, strlen(cmd_buf));

  memset(ctx->modem_test_response, 0, sizeof(ctx->modem_test_response));
  ssize_t n = read(mfd, ctx->modem_test_response, sizeof(ctx->modem_test_response) - 1);
  close(mfd);

  ctx->last_test_modem_valid = true;
  if (n > 0) {
    // Trim trailing newlines for clean GUI display
    size_t len = strlen(ctx->modem_test_response);
    while (len > 0 && (ctx->modem_test_response[len - 1] == '\r' ||
                       ctx->modem_test_response[len - 1] == '\n')) {
      ctx->modem_test_response[--len] = '\0';
    }
    return 0;
  }

  snprintf(ctx->modem_test_response, sizeof(ctx->modem_test_response), "NO RESPONSE");
  return -1;
}
