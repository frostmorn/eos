#include "includes.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/kcmd.h"
#include "ecore/usb.h"
#include "ebin/usbctl/usbctl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// ── Subcommand Handlers ──────────────────────────────────────

static int subcmd_status(int argc, char **argv, int fd) {
  (void)argc;
  (void)argv;
  eos_usb_bus_status_t st;
  memset(&st, 0, sizeof(st));
  if (ioctl(fd, EOS_USB_IOCTL_GET_STATUS, &st) < 0) {
    perror("usbctl: ioctl GET_STATUS failed");
    return 1;
  }

  const char *role_str = "UNKNOWN";
  if (st.active_role == EOS_USB_ROLE_HOST) {
    role_str = "HOST (Master)";
  } else if (st.active_role == EOS_USB_ROLE_DEVICE_COMPOSITE) {
    role_str = "Composite Device";
  } else if (st.active_role == EOS_USB_ROLE_DEBUG_USJ) {
    role_str = "DEBUG_USJ (JTAG+CDC Console)";
  } else if (st.active_role == EOS_USB_ROLE_OFF) {
    role_str = "OFF";
  }

  const char *speed_str = "UNKNOWN";
  if (st.speed == EOS_USB_SPEED_FULL) {
    speed_str = "Full Speed (12 Mbps)";
  } else if (st.speed == EOS_USB_SPEED_HIGH) {
    speed_str = "High Speed (480 Mbps)";
  } else if (st.speed == EOS_USB_SPEED_LOW) {
    speed_str = "Low Speed (1.5 Mbps)";
  }

  printf("USB Subsystem Status (Port %u):\n", (unsigned int)st.port_id);
  printf("  Port:       %u\n", (unsigned int)st.port_id);
  printf("  Role:       %s\n", role_str);
  printf("  Speed:      %s\n", speed_str);
  printf("  VBUS:       %s\n", st.vbus_present ? "Present" : "Absent");
  printf("  Host Dev:   %s\n", st.host_device_connected ? "Connected" : "None");
  printf("  Functions:  0x%02lx (SERIAL:%s, MODEM:%s, MSC:%s, MTP:%s)\n",
         (unsigned long)st.gadget_funcs_enabled,
         (st.gadget_funcs_enabled & EOS_USB_GADGET_FUNC_SERIAL) ? "ON" : "OFF",
         (st.gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MODEM) ? "ON" : "OFF",
         (st.gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MSC) ? "ON" : "OFF",
         (st.gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MTP) ? "ON" : "OFF");
  printf("  MSC Backing:%s\n", st.msc_backing_dev[0] ? st.msc_backing_dev : "None");
  printf("  MTP Path:   %s\n", st.mtp_base_path[0] ? st.mtp_base_path : "None");
  printf("  RX/TX:      %lu / %lu bytes\n", (unsigned long)st.rx_bytes, (unsigned long)st.tx_bytes);
  return 0;
}

static const char *role_enum_to_kcmd(eos_usb_role_t role) {
  switch (role) {
    case EOS_USB_ROLE_HOST:             return "host";
    case EOS_USB_ROLE_DEVICE_COMPOSITE: return "device";
    case EOS_USB_ROLE_DEBUG_USJ:        return "debug";
    default:                            return "off";
  }
}

static void usbctl_build_role_kcmd(int fd, const char *target, char *out, size_t out_sz) {
  if (access("/dev/usb1", F_OK) != 0) {
    strlcpy(out, target, out_sz);
    return;
  }
  eos_usb_bus_status_t st;
  memset(&st, 0, sizeof(st));
  if (ioctl(fd, EOS_USB_IOCTL_GET_STATUS, &st) < 0) {
    strlcpy(out, target, out_sz);
    return;
  }
  bool is_p1 = (st.port_id == 1);
  const char *other_path = is_p1 ? "/dev/usb0" : "/dev/usb1";
  const char *other_str = is_p1 ? "debug" : "device";
  int ofd = open(other_path, O_RDONLY);
  if (ofd >= 0) {
    eos_usb_role_t orole = EOS_USB_ROLE_OFF;
    if (ioctl(ofd, EOS_USB_IOCTL_GET_ROLE, &orole) == 0) {
      other_str = role_enum_to_kcmd(orole);
    }
    close(ofd);
  }
  snprintf(out, out_sz, "%s %s", is_p1 ? other_str : target, is_p1 ? target : other_str);
}

static int subcmd_role(int argc, char **argv, int fd) {
  if (argc < 3) {
    eos_usb_role_t role = EOS_USB_ROLE_OFF;
    if (ioctl(fd, EOS_USB_IOCTL_GET_ROLE, &role) < 0) {
      perror("usbctl: ioctl GET_ROLE failed");
      return 1;
    }
    const char *rname = (role == EOS_USB_ROLE_HOST) ? "HOST" :
                        (role == EOS_USB_ROLE_DEVICE_COMPOSITE) ? "DEVICE_COMPOSITE" :
                        (role == EOS_USB_ROLE_DEBUG_USJ) ? "DEBUG_USJ" : "OFF";
    printf("Active USB Role: %d (%s)\n", (int)role, rname);
    return 0;
  }

  const char *target = argv[2];
  bool do_reboot = (argc >= 4 && strcmp(argv[3], "--reboot") == 0);
  eos_usb_role_t new_role = EOS_USB_ROLE_OFF;
  if (strcmp(target, "host") == 0) {
    new_role = EOS_USB_ROLE_HOST;
  } else if (strcmp(target, "device") == 0 || strcmp(target, "composite") == 0) {
    new_role = EOS_USB_ROLE_DEVICE_COMPOSITE;
  } else if (strcmp(target, "debug") == 0 || strcmp(target, "jtag") == 0 || strcmp(target, "usj") == 0) {
    new_role = EOS_USB_ROLE_DEBUG_USJ;
  } else if (strcmp(target, "off") == 0) {
    new_role = EOS_USB_ROLE_OFF;
  } else {
    fprintf(stderr, "usbctl: unknown role '%s' (use debug, composite, host, or off)\n", target);
    return 1;
  }

  if (ioctl(fd, EOS_USB_IOCTL_SET_ROLE, new_role) < 0) {
    if (errno == ENOTSUP) {
      if (do_reboot) {
        char kcmd_buf[EOS_KCMD_MAX_LEN] = {0};
        usbctl_build_role_kcmd(fd, target, kcmd_buf, sizeof(kcmd_buf));
        printf("Reboot required; rebooting with role '%s'...\n", kcmd_buf);
        fflush(stdout);
        eos_reboot_with_kcmd(kcmd_buf);
        return 0;
      }
      fprintf(stderr,
              "usbctl: reboot required to apply role '%s' on this port "
              "(run 'usbctl ... role %s --reboot' or 'reboot -- %s')\n",
              target, target, target);
      return 1;
    }
    perror("usbctl: ioctl SET_ROLE failed");
    return 1;
  }
  printf("Switched USB role to '%s'\n", target);
  if (do_reboot) {
    char kcmd_buf[EOS_KCMD_MAX_LEN] = {0};
    usbctl_build_role_kcmd(fd, target, kcmd_buf, sizeof(kcmd_buf));
    eos_reboot_with_kcmd(kcmd_buf);
  }
  return 0;
}

static int subcmd_funcs(int argc, char **argv, int fd) {
  if (argc < 3) {
    uint32_t mask = 0;
    if (ioctl(fd, EOS_USB_IOCTL_GADGET_GET_FUNCS, &mask) < 0) {
      perror("usbctl: ioctl GADGET_GET_FUNCS failed");
      return 1;
    }
    printf("USB Functions Mask: 0x%02lx\n", (unsigned long)mask);
    return 0;
  }

  uint32_t new_mask = (uint32_t)strtoul(argv[2], NULL, 0);
  if (ioctl(fd, EOS_USB_IOCTL_GADGET_SET_FUNCS, new_mask) < 0) {
    perror("usbctl: ioctl GADGET_SET_FUNCS failed");
    return 1;
  }
  printf("Updated gadget functions mask to 0x%02lx\n", (unsigned long)new_mask);
  return 0;
}

static int subcmd_msc(int argc, char **argv, int fd) {
  if (argc < 3) {
    char dev_path[EOS_USBCTL_PATH_BUF_LEN];
    memset(dev_path, 0, sizeof(dev_path));
    if (ioctl(fd, EOS_USB_IOCTL_GADGET_GET_MSC_DEV, dev_path) < 0) {
      perror("usbctl: ioctl GADGET_GET_MSC_DEV failed");
      return 1;
    }
    printf("MSC Backing Device: %s\n", dev_path[0] ? dev_path : "None");
    return 0;
  }

  const char *new_path = argv[2];
  if (ioctl(fd, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, new_path) < 0) {
    perror("usbctl: ioctl GADGET_SET_MSC_DEV failed");
    return 1;
  }
  printf("Bound MSC backing device to '%s'\n", new_path);
  return 0;
}

static int subcmd_mtp(int argc, char **argv, int fd) {
  if (argc < 3) {
    char mtp_path[EOS_USBCTL_PATH_BUF_LEN];
    memset(mtp_path, 0, sizeof(mtp_path));
    if (ioctl(fd, EOS_USB_IOCTL_GADGET_GET_MTP_PATH, mtp_path) < 0) {
      perror("usbctl: ioctl GADGET_GET_MTP_PATH failed");
      return 1;
    }
    printf("MTP Export Path: %s\n", mtp_path[0] ? mtp_path : "None");
    return 0;
  }

  const char *new_path = argv[2];
  if (ioctl(fd, EOS_USB_IOCTL_GADGET_SET_MTP_PATH, new_path) < 0) {
    perror("usbctl: ioctl GADGET_SET_MTP_PATH failed");
    return 1;
  }
  printf("Configured MTP export path to '%s'\n", new_path);
  return 0;
}

static int subcmd_storage(int argc, char **argv, int fd) {
  if (argc < 3) {
    printf("Usage: usbctl storage [mtp [path] | msc [dev]]\n");
    return 1;
  }
  if (strcmp(argv[2], "mtp") == 0) {
    const char *path = (argc >= 4) ? argv[3] : EOS_USB_DEFAULT_MTP_PATH;
    (void)ioctl(fd, EOS_USB_IOCTL_GADGET_SET_MTP_PATH, path);
    uint32_t mask = 0;
    (void)ioctl(fd, EOS_USB_IOCTL_GADGET_GET_FUNCS, &mask);
    mask &= ~EOS_USB_GADGET_FUNC_MSC;
    mask |= EOS_USB_GADGET_FUNC_MTP;
    if (ioctl(fd, EOS_USB_IOCTL_GADGET_SET_FUNCS, mask) < 0) {
      perror("usbctl: failed to switch to MTP storage");
      return 1;
    }
    printf("Switched USB storage to MTP (Shared VFS: %s)\n", path);
    return 0;
  }
  if (strcmp(argv[2], "msc") == 0) {
    const char *dev = (argc >= 4) ? argv[3] : EOS_USB_DEFAULT_MSC_BACKING;
    (void)ioctl(fd, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, dev);
    uint32_t mask = 0;
    (void)ioctl(fd, EOS_USB_IOCTL_GADGET_GET_FUNCS, &mask);
    mask &= ~EOS_USB_GADGET_FUNC_MTP;
    mask |= EOS_USB_GADGET_FUNC_MSC;
    if (ioctl(fd, EOS_USB_IOCTL_GADGET_SET_FUNCS, mask) < 0) {
      perror("usbctl: failed to switch to MSC storage");
      return 1;
    }
    printf("Switched USB storage to MSC (Exclusive Block Dev: %s)\n", dev);
    return 0;
  }
  printf("Unknown storage mode '%s'. Use 'mtp' or 'msc'.\n", argv[2]);
  return 1;
}

static int subcmd_serial(int argc, char **argv, int fd) {
  (void)fd;
  int sfd = open(EOS_USBCTL_SERIAL_DEV, O_RDWR);
  if (sfd < 0) {
    perror("usbctl: failed to open " EOS_USBCTL_SERIAL_DEV);
    return 1;
  }

  const char *msg = (argc >= 3) ? argv[2] : EOS_USBCTL_SERIAL_TEST_MSG;
  write(sfd, msg, strlen(msg));

  char rx[EOS_USBCTL_DATA_BUF_LEN];
  memset(rx, 0, sizeof(rx));
  ssize_t n = read(sfd, rx, sizeof(rx) - 1);
  close(sfd);

  if (n > 0) {
    printf("Serial Echo: %s (PASS)\n", rx);
    return 0;
  }
  printf("Serial: No data received\n");
  return 1;
}

static int subcmd_modem(int argc, char **argv, int fd) {
  (void)fd;
  int modem_fd = open(EOS_USBCTL_MODEM_DEV, O_RDWR);
  if (modem_fd < 0) {
    perror("usbctl: failed to open " EOS_USBCTL_MODEM_DEV);
    return 1;
  }

  const char *cmd = (argc >= 3) ? argv[2] : "AT\r\n";
  char cmd_buf[EOS_USBCTL_LINE_BUF_LEN];
  snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", cmd);

  write(modem_fd, cmd_buf, strlen(cmd_buf));

  char resp[EOS_USBCTL_DATA_BUF_LEN];
  memset(resp, 0, sizeof(resp));
  ssize_t n = read(modem_fd, resp, sizeof(resp) - 1);
  if (n > 0) {
    printf("Modem Response: %s", resp);
  } else {
    printf("Modem: No response received\n");
  }

  close(modem_fd);
  return 0;
}

static int subcmd_host(int argc, char **argv, int fd) {
  if (argc >= 3 && strcmp(argv[2], "reset") == 0) {
    if (ioctl(fd, EOS_USB_IOCTL_HOST_RESET_PORT, 0) < 0) {
      perror("usbctl: host reset port failed");
      return 1;
    }
    printf("Drove hardware reset on host port 0\n");
    return 0;
  }

  if (ioctl(fd, EOS_USB_IOCTL_HOST_ENUM) < 0) {
    perror("usbctl: host enum failed");
    return 1;
  }

  eos_usb_host_dev_list_t list;
  memset(&list, 0, sizeof(list));
  if (ioctl(fd, EOS_USB_IOCTL_HOST_GET_DEV_LIST, &list) < 0) {
    perror("usbctl: host get dev list failed");
    return 1;
  }

  printf("Host Downstream Devices (%lu found):\n", (unsigned long)list.count);
  for (uint32_t i = 0; i < list.count; i++) {
    printf("  [%u] Addr:%u VID:0x%04x PID:0x%04x Class:%s\n",
           (unsigned int)i,
           (unsigned int)list.devices[i].dev_addr,
           (unsigned int)list.devices[i].vid,
           (unsigned int)list.devices[i].pid,
           list.devices[i].class_name);
  }
  return 0;
}

// ── Subcommand Dispatch Table ────────────────────────────────

static const eos_usbctl_subcmd_t s_subcmds[] = {
    {"status", "Show USB subsystem status and metrics", subcmd_status},
    {"role",   "Get or set active role (debug, device, host, off)", subcmd_role},
    {"funcs",  "Get or set gadget function bitmask", subcmd_funcs},
    {"msc",    "Get or set MSC backing block device", subcmd_msc},
    {"mtp",    "Get or set MTP VFS export path", subcmd_mtp},
    {"storage","Select USB storage mode (mtp [path] | msc [dev])", subcmd_storage},
    {"serial", "Send test data to CDC-ACM serial and verify echo", subcmd_serial},
    {"modem",  "Send AT command to virtual CDC-ACM modem", subcmd_modem},
    {"host",   "Enumerate downstream host devices or reset port", subcmd_host},
};

static void print_usage(void) {
  printf("Usage: usbctl [-d /dev/usbX | -c 0|1] <command> [args...]\n\nCommands:\n");
  size_t count = sizeof(s_subcmds) / sizeof(s_subcmds[0]);
  for (size_t i = 0; i < count; i++) {
    printf("  %-8s %s\n", s_subcmds[i].name, s_subcmds[i].help);
  }
}

static bool controller_exists(const char *path) {
  int tfd = open(path, O_RDWR);
  if (tfd >= 0) {
    close(tfd);
    return true;
  }
  return false;
}

// ── Main Entry Point ─────────────────────────────────────────

int usbctl_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 0;
  }

  const char *target_dev = NULL;
  int arg_idx = 1;

  if (argc >= 3 && strcmp(argv[1], "-d") == 0) {
    target_dev = argv[2];
    arg_idx = 3;
  } else if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
    target_dev = (atoi(argv[2]) == 1) ? "/dev/usb1" : "/dev/usb0";
    arg_idx = 3;
  }

  if (arg_idx >= argc) {
    print_usage();
    return 0;
  }

  const char *cmd_name = argv[arg_idx];
  size_t count = sizeof(s_subcmds) / sizeof(s_subcmds[0]);
  const eos_usbctl_subcmd_t *selected = NULL;

  for (size_t i = 0; i < count; i++) {
    if (strcmp(cmd_name, s_subcmds[i].name) == 0) {
      selected = &s_subcmds[i];
      break;
    }
  }

  if (!selected) {
    fprintf(stderr, "usbctl: unknown command '%s'\n", cmd_name);
    print_usage();
    return 1;
  }

  // If no controller explicitly specified and status requested, dump all available ports
  if (!target_dev && strcmp(cmd_name, "status") == 0 && controller_exists("/dev/usb1")) {
    int fd0 = open("/dev/usb0", O_RDWR);
    if (fd0 >= 0) {
      subcmd_status(argc - arg_idx + 1, &argv[arg_idx - 1], fd0);
      close(fd0);
    }
    int fd1 = open("/dev/usb1", O_RDWR);
    if (fd1 >= 0) {
      printf("\n");
      subcmd_status(argc - arg_idx + 1, &argv[arg_idx - 1], fd1);
      close(fd1);
    }
    return 0;
  }

  // If no controller explicitly specified, default to /dev/usb1 if present (composite gadget host on P4), else /dev/usb0
  if (!target_dev) {
    if (controller_exists("/dev/usb1")) {
      target_dev = "/dev/usb1";
    } else {
      target_dev = "/dev/usb0";
    }
  }

  int fd = open(target_dev, O_RDWR);
  if (fd < 0) {
    if (strcmp(cmd_name, "serial") != 0 && strcmp(cmd_name, "modem") != 0) {
      perror("usbctl: failed to open controller");
      return 1;
    }
  }

  int ret = selected->handler(argc - arg_idx + 1, &argv[arg_idx - 1], fd);
  if (fd >= 0) {
    close(fd);
  }
  return ret;
}

EOS_BIN_ATTR const eos_bin_t usbctl_bin = {
    EOS_BIN_INITIALIZER,
    .flags = 0,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "usbctl",
    .name = "USB Control",
    .group = "Hardware",
    .description = "EOS USB Subsystem control and testing utility",
    .entry_point = usbctl_main,
};
