#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"

static const char *battery_status_str(eos_battery_status_t st) {
  switch (st) {
  case EOS_BATTERY_STATUS_CHARGING:
    return "Charging";
  case EOS_BATTERY_STATUS_FULL:
    return "Full";
  case EOS_BATTERY_STATUS_DISCHARGING:
    return "Discharging";
  case EOS_BATTERY_STATUS_NOT_PRESENT:
  default:
    return "Not Present";
  }
}

int battery_main(int argc, char **argv) {
  const char *path = "/dev/battery0";
  bool show_raw = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: battery [-r|--raw] [path]\n");
      printf("  -r, --raw    Print raw /dev/battery0 contents\n");
      printf("  path         Custom battery device path (default: /dev/battery0)\n");
      return 0;
    } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--raw") == 0) {
      show_raw = true;
    } else if (argv[i][0] == '/') {
      path = argv[i];
    }
  }

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("battery: failed to open %s: %s\n", path, strerror(errno));
    return 1;
  }

  if (show_raw) {
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      printf("%s", buf);
    } else {
      printf("battery: empty read from %s\n", path);
    }
    close(fd);
    return 0;
  }

  eos_battery_info_t info;
  if (ioctl(fd, EOS_POWER_IOCTL_GET_BATTERY_INFO, &info) == 0) {
    if (info.status == EOS_BATTERY_STATUS_NOT_PRESENT) {
      printf("Battery: Not connected (running on USB power)\n");
    } else {
      printf("Battery: %ld%% [%lu mV] Status: %s (Empty: %lu mV, Full: %lu mV)\n",
             (long)info.soc_percent, (unsigned long)info.voltage_mv,
             battery_status_str(info.status),
             (unsigned long)info.v_empty_mv, (unsigned long)info.v_full_mv);
    }
  } else {
    // Fallback to read() if ioctl fails
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      printf("%s", buf);
    } else {
      printf("battery: failed to get battery status from %s: %s\n", path,
             strerror(errno));
      close(fd);
      return 1;
    }
  }

  close(fd);
  return 0;
}

EOS_BIN_ATTR eos_bin_t battery = {
    EOS_BIN_INITIALIZER,
    .filename = "battery",
    .name = "Battery Monitor",
    .group = "System",
    .description = "Inspect battery state of charge and voltage",
    .entry_point = battery_main};
