#include "ebin/lvgl_usb/usb_logic.h"
#include "ecore/ioctl.h"
#include "ecore/usb.h"

#include <dirent.h>
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
    snprintf(buf, buflen, "Composite Device");
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
    strncpy(ctx->mtp_path, ctx->status.mtp_base_path, sizeof(ctx->mtp_path) - 1);
    ctx->mtp_path[sizeof(ctx->mtp_path) - 1] = '\0';
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
  if (usb_logic_select_dev(ctx, "/dev/usb1") == 0) {
    return 0;
  }
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
    return (errno == ENOTSUP) ? -ENOTSUP : -1;
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

int usb_logic_set_mtp_path(usb_logic_ctx_t *ctx, const char *path) {
  if (!ctx || ctx->fd < 0 || !path) return -1;
  if (ioctl(ctx->fd, EOS_USB_IOCTL_GADGET_SET_MTP_PATH, path) < 0) {
    return -1;
  }
  return usb_logic_refresh(ctx);
}

int usb_logic_set_storage_mode(usb_logic_ctx_t *ctx, bool mtp_mode) {
  if (!ctx || ctx->fd < 0) return -1;
  uint32_t mask = ctx->gadget_mask;
  if (mtp_mode) {
    mask &= ~EOS_USB_GADGET_FUNC_MSC;
    mask |= EOS_USB_GADGET_FUNC_MTP;
  } else {
    mask &= ~EOS_USB_GADGET_FUNC_MTP;
    mask |= EOS_USB_GADGET_FUNC_MSC;
  }
  if (ioctl(ctx->fd, EOS_USB_IOCTL_GADGET_SET_FUNCS, mask) < 0) {
    return -1;
  }
  return usb_logic_refresh(ctx);
}

int usb_logic_cycle_msc_device(usb_logic_ctx_t *ctx) {
  if (!ctx) return -1;
  char devices[EOS_USB_LOGIC_MAX_MSC_DEVS][EOS_USB_MSC_MAX_BACKING_DEV_LEN];
  int count = 0;

  DIR *dir = opendir("/dev");
  if (dir) {
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < EOS_USB_LOGIC_MAX_MSC_DEVS) {
      if (ent->d_name[0] == '.' || strncmp(ent->d_name, "usb", 3) == 0 ||
          strlen(ent->d_name) + sizeof("/dev/") > sizeof(devices[0])) {
        continue;
      }
      char path[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
      memcpy(path, "/dev/", sizeof("/dev/") - 1);
      strlcpy(path + sizeof("/dev/") - 1, ent->d_name, sizeof(path) - (sizeof("/dev/") - 1));
      int dfd = open(path, O_RDONLY);
      if (dfd >= 0) {
        uint32_t cap = 0;
        uint32_t sec_sz = 0;
        if (ioctl(dfd, EOS_STORAGE_IOCTL_GET_SECTOR_SIZE, &sec_sz) == 0 && sec_sz > 0 &&
            ioctl(dfd, EOS_STORAGE_IOCTL_GET_CAPACITY, &cap) == 0 && cap > 0) {
          strncpy(devices[count], path, EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1);
          devices[count][EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1] = '\0';
          count++;
        }
        close(dfd);
      }
    }
    closedir(dir);
  }

  if (count == 0) {
    return -1;
  }

  int cur_idx = -1;
  for (int i = 0; i < count; i++) {
    if (strcmp(ctx->msc_backing, devices[i]) == 0) {
      cur_idx = i;
      break;
    }
  }
  int next_idx = (cur_idx + 1) % count;
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
  char rx[EOS_USB_LOGIC_MAX_RESP];
  ssize_t nr = -1;
  if (nw == (ssize_t)strlen(msg)) {
    for (int attempt = 0; attempt < EOS_USB_LOGIC_SERIAL_POLL_TRIES; attempt++) {
      nr = read(sfd, rx, sizeof(rx) - 1);
      if (nr >= 0) {
        break;
      }
      usleep(EOS_USB_LOGIC_SERIAL_POLL_US);
    }
  }
  close(sfd);

  ctx->last_test_serial_valid = true;
  ctx->serial_test_passed = (nw == (ssize_t)strlen(msg) && nr >= 0);
  return ctx->serial_test_passed ? 0 : -1;
}

int usb_logic_run_modem_ping(usb_logic_ctx_t *ctx, const char *cmd) {
  if (!ctx) return -1;
  int mfd = open(EOS_USB_LOGIC_MODEM_DEV, O_RDWR);
  if (mfd < 0) {
    ctx->last_test_modem_valid = true;
    snprintf(ctx->modem_test_response, sizeof(ctx->modem_test_response), "ERR: OPEN_FAIL");
    return -1;
  }

  const char *to_send = (cmd && cmd[0]) ? cmd : "ATI";
  size_t slen = strlen(to_send);
  char cmd_buf[EOS_USB_LOGIC_MAX_RESP];
  if (slen > 0 && to_send[slen - 1] == '\n') {
    snprintf(cmd_buf, sizeof(cmd_buf), "%s", to_send);
  } else {
    snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", to_send);
  }
  ssize_t nw = write(mfd, cmd_buf, strlen(cmd_buf));
  if (nw < 0) {
    close(mfd);
    ctx->last_test_modem_valid = true;
    snprintf(ctx->modem_test_response, sizeof(ctx->modem_test_response), "ERR: WRITE_FAIL");
    return -1;
  }

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
