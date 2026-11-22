#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "emisc/strlimits.h"
#include "pwmctl.h"

static void pwmctl_normalize_path(const char *in_dev, char *out_buf, size_t max_len) {
  if (in_dev[0] == '/') {
    snprintf(out_buf, max_len, "%s", in_dev);
  } else {
    snprintf(out_buf, max_len, "/dev/%s", in_dev);
  }
}

static void pwmctl_print_usage(void) {
  printf("Usage: pwmctl <command> [options]\n\n");
  printf("Commands:\n");
  printf("  list                          List active PWM devices and hardware capabilities\n");
  printf("  get <dev>                     Get current duty cycle percentage\n");
  printf("  set <dev> <pct>               Set duty cycle percentage (0 - 100)\n");
  printf("  freq <dev> <hz>               Set channel frequency in Hz\n");
  printf("  fade <dev> <pct> <ms>         Smoothly fade duty cycle to target percentage\n");
  printf("  help, -h, --help              Show this help message\n");
}

static int pwmctl_cmd_list(void) {
  printf("%-12s %-8s %-8s %-6s %-6s %-6s %s\n", "DEVICE", "FREQ", "DUTY", "TIMER", "CHAN", "PIN", "STATUS");
  printf("%-12s %-8s %-8s %-6s %-6s %-6s %s\n", "------", "----", "----", "-----", "----", "---", "------");

  int found = 0;
  for (int i = 0; i < EOS_PWMCTL_MAX_DEV_SCAN; i++) {
    char dev_path[EOS_SMALL_STR_LEN];
    snprintf(dev_path, sizeof(dev_path), "/dev/pwm%d", i);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) continue;

    eos_pwm_caps_t caps;
    memset(&caps, 0, sizeof(caps));
    if (ioctl(fd, EOS_PWM_IOCTL_GET_CAPS, &caps) == 0) {
      char duty_str[EOS_NUM_STR_LEN] = "0%";
      ssize_t n = read(fd, duty_str, sizeof(duty_str) - 1);
      if (n > 0) {
        duty_str[n] = '\0';
        char *nl = strchr(duty_str, '\n');
        if (nl) *nl = '\0';
      }
      printf("%-12s %-8" PRIu32 " %-8s %-6" PRId32 " %-6" PRId32 " %-6" PRId32 " %s\n",
             dev_path, caps.freq_hz, duty_str, caps.timer_idx, caps.channel_idx, caps.pin,
             caps.enabled ? "enabled" : "disabled");
      found++;
    }
    close(fd);
  }

  if (found == 0) {
    printf("(no active PWM devices found in /dev/pwm*)\n");
  }
  return 0;
}

static int pwmctl_cmd_get(const char *dev_name) {
  char dev_path[EOS_SMALL_STR_LEN];
  pwmctl_normalize_path(dev_name, dev_path, sizeof(dev_path));

  int fd = open(dev_path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "pwmctl: failed to open %s: %s\n", dev_path, strerror(errno));
    return 1;
  }

  char buf[EOS_PWMCTL_DECIMAL_BASE + 2];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (n <= 0) {
    fprintf(stderr, "pwmctl: failed to read duty from %s\n", dev_path);
    return 1;
  }
  buf[n] = '\0';
  char *nl = strchr(buf, '\n');
  if (nl) *nl = '\0';
  printf("%s: %s%%\n", dev_path, buf);
  return 0;
}

static int pwmctl_cmd_set(const char *dev_name, const char *val_str) {
  char dev_path[EOS_SMALL_STR_LEN];
  pwmctl_normalize_path(dev_name, dev_path, sizeof(dev_path));

  char *endptr = NULL;
  long pct = strtol(val_str, &endptr, EOS_PWMCTL_DECIMAL_BASE);
  if (endptr == val_str || pct < 0 || pct > EOS_PWMCTL_MAX_DUTY_PCT) {
    fprintf(stderr, "pwmctl: invalid duty percentage '%s' (0 - 100)\n", val_str);
    return 1;
  }

  int fd = open(dev_path, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "pwmctl: failed to open %s: %s\n", dev_path, strerror(errno));
    return 1;
  }

  int ret = ioctl(fd, EOS_PWM_IOCTL_SET_DUTY, (uint32_t)pct);
  close(fd);

  if (ret != 0) {
    fprintf(stderr, "pwmctl: set duty on %s failed: %s\n", dev_path, strerror(errno));
    return 1;
  }
  printf("%s: duty set to %ld%%\n", dev_path, pct);
  return 0;
}

static int pwmctl_cmd_freq(const char *dev_name, const char *val_str) {
  char dev_path[EOS_SMALL_STR_LEN];
  pwmctl_normalize_path(dev_name, dev_path, sizeof(dev_path));

  char *endptr = NULL;
  long hz = strtol(val_str, &endptr, EOS_PWMCTL_DECIMAL_BASE);
  if (endptr == val_str || hz <= 0) {
    fprintf(stderr, "pwmctl: invalid frequency '%s'\n", val_str);
    return 1;
  }

  int fd = open(dev_path, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "pwmctl: failed to open %s: %s\n", dev_path, strerror(errno));
    return 1;
  }

  int ret = ioctl(fd, EOS_PWM_IOCTL_SET_FREQ, (uint32_t)hz);
  close(fd);

  if (ret != 0) {
    fprintf(stderr, "pwmctl: set frequency on %s failed: %s\n", dev_path, strerror(errno));
    return 1;
  }
  printf("%s: frequency set to %ld Hz\n", dev_path, hz);
  return 0;
}

static int pwmctl_cmd_fade(const char *dev_name, const char *pct_str, const char *ms_str) {
  char dev_path[EOS_SMALL_STR_LEN];
  pwmctl_normalize_path(dev_name, dev_path, sizeof(dev_path));

  char *end_pct = NULL;
  long pct = strtol(pct_str, &end_pct, EOS_PWMCTL_DECIMAL_BASE);
  char *end_ms = NULL;
  long ms = strtol(ms_str, &end_ms, EOS_PWMCTL_DECIMAL_BASE);

  if (end_pct == pct_str || pct < 0 || pct > EOS_PWMCTL_MAX_DUTY_PCT || end_ms == ms_str || ms < 0) {
    fprintf(stderr, "pwmctl: invalid fade arguments (target pct 0-100, duration ms >= 0)\n");
    return 1;
  }

  int fd = open(dev_path, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "pwmctl: failed to open %s: %s\n", dev_path, strerror(errno));
    return 1;
  }

  eos_pwm_fade_t fade = {
      .target_duty = (uint16_t)(((uint32_t)pct * 65535U) / 100U),
      .duration_ms = (uint32_t)ms,
  };
  int ret = ioctl(fd, EOS_PWM_IOCTL_FADE, &fade);
  close(fd);

  if (ret != 0) {
    fprintf(stderr, "pwmctl: fade on %s failed: %s\n", dev_path, strerror(errno));
    return 1;
  }
  printf("%s: fading to %ld%% over %ld ms\n", dev_path, pct, ms);
  return 0;
}

int pwmctl_main(int argc, char **argv) {
  if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    pwmctl_print_usage();
    return 0;
  }

  const char *cmd = argv[1];
  if (strcmp(cmd, "list") == 0) {
    return pwmctl_cmd_list();
  }
  if (strcmp(cmd, "get") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: pwmctl get <dev>\n");
      return 1;
    }
    return pwmctl_cmd_get(argv[2]);
  }
  if (strcmp(cmd, "set") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: pwmctl set <dev> <pct>\n");
      return 1;
    }
    return pwmctl_cmd_set(argv[2], argv[3]);
  }
  if (strcmp(cmd, "freq") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: pwmctl freq <dev> <hz>\n");
      return 1;
    }
    return pwmctl_cmd_freq(argv[2], argv[3]);
  }
  if (strcmp(cmd, "fade") == 0) {
    if (argc < 5) {
      fprintf(stderr, "Usage: pwmctl fade <dev> <pct> <ms>\n");
      return 1;
    }
    return pwmctl_cmd_fade(argv[2], argv[3], argv[4]);
  }

  fprintf(stderr, "pwmctl: unknown command '%s'. Run 'pwmctl help' for usage.\n", cmd);
  return 1;
}

EOS_BIN_ATTR eos_bin_t pwmctl = {
    EOS_BIN_INITIALIZER,
    .filename    = "pwmctl",
    .name        = "PWM Control",
    .group       = "Hardware",
    .description = "Control hardware PWM channels, duty cycle and hardware fading",
    .entry_point = pwmctl_main,
};
