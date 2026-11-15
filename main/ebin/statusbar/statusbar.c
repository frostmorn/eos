#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ebin/bar_common.h"
#include "ebin/lvfont_render.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define SBAR_MAX_LINE 128

typedef struct {
  char line1[SBAR_MAX_LINE];
  char line2[SBAR_MAX_LINE];
} sbar_content_t;

static bool read_sbar_file(sbar_content_t *out) {
  char buf[SBAR_MAX_LINE * 2] = {0};
  if (bar_read_file("/tmp/sbar", buf, sizeof(buf)) <= 0) {
    return false;
  }

  char *line1 = NULL;
  char *line2 = NULL;
  bar_split_lines(buf, &line1, &line2);

  const char *text = NULL;
  if (!bar_validate_pid_prefix(line1, &text)) {
    return false;
  }

  snprintf(out->line1, sizeof(out->line1), "%.*s", (int)(sizeof(out->line1) - 1), text);
  if (line2 && line2[0] != '\0') {
    snprintf(out->line2, sizeof(out->line2), "%.*s", (int)(sizeof(out->line2) - 1), line2);
  } else {
    out->line2[0] = '\0';
  }
  return true;
}

static void read_fallback_stats(sbar_content_t *out) {
  uint32_t mem_free_kb = 0;
  uint32_t mem_int_kb = 0;
  uint32_t mem_spi_kb = 0;

  char pbuf[384];
  if (bar_read_file("/proc/meminfo", pbuf, sizeof(pbuf)) > 0) {
    char *line = pbuf;
    while (line && *line) {
      char *next_line = strchr(line, '\n');
      if (next_line) {
        *next_line = '\0';
        next_line++;
      }
      uint32_t val = 0;
      if (sscanf(line, "MemFree: %lu kB", (unsigned long *)&val) == 1) {
        mem_free_kb = val;
      } else if (sscanf(line, "InternalFree: %lu kB", (unsigned long *)&val) == 1) {
        mem_int_kb = val;
      } else if (sscanf(line, "SPIRAMFree: %lu kB", (unsigned long *)&val) == 1) {
        mem_spi_kb = val;
      }
      line = next_line;
    }
  }

  uint64_t uptime_sec = 0;
  if (bar_read_file("/proc/uptime", pbuf, sizeof(pbuf)) > 0) {
    double up_val = 0;
    if (sscanf(pbuf, "%lf", &up_val) == 1) {
      uptime_sec = (uint64_t)up_val;
    }
  }

  unsigned int up_h = (unsigned int)(uptime_sec / 3600);
  unsigned int up_m = (unsigned int)((uptime_sec % 3600) / 60);
  unsigned int up_s = (unsigned int)(uptime_sec % 60);

  // Read load and task count from /proc/loadavg
  unsigned int task_count = 0;
  float load1 = 0.0f;
  if (bar_read_file("/proc/loadavg", pbuf, sizeof(pbuf)) > 0) {
    (void)sscanf(pbuf, "%f %*f %*f 1/%u", &load1, &task_count);
  }

  // Battery charge status
  char bat_str[32] = {0};
  int bfd = open("/dev/battery0", O_RDONLY);
  if (bfd >= 0) {
    eos_battery_info_t binfo;
    if (ioctl(bfd, EOS_POWER_IOCTL_GET_BATTERY_INFO, &binfo) == 0) {
      if (binfo.status == EOS_BATTERY_STATUS_NOT_PRESENT || binfo.voltage_mv < 500) {
        snprintf(bat_str, sizeof(bat_str), "%s", LV_SYMBOL_CHARGE);
      } else {
        const char *bsym = (binfo.soc_percent >= 80) ? LV_SYMBOL_BATTERY_FULL
                         : (binfo.soc_percent >= 55) ? LV_SYMBOL_BATTERY_3
                         : (binfo.soc_percent >= 30) ? LV_SYMBOL_BATTERY_2
                         : (binfo.soc_percent >= 10) ? LV_SYMBOL_BATTERY_1
                                                     : LV_SYMBOL_BATTERY_EMPTY;
        snprintf(bat_str, sizeof(bat_str), "%s %u%%", bsym, (unsigned int)binfo.soc_percent);
      }
    }
    close(bfd);
  }
  if (!bat_str[0]) {
    snprintf(bat_str, sizeof(bat_str), "%s", LV_SYMBOL_CHARGE);
  }

  // WLAN status
  char wlan_str[32] = {0};
  int wfd = open("/dev/sta0", O_RDONLY);
  if (wfd >= 0) {
    eos_netif_info_t ninfo;
    if (ioctl(wfd, EOS_NETIF_IOCTL_GET_INFO, &ninfo) == 0) {
      if ((ninfo.flags & EOS_NETIF_FLAG_LINK_UP) && ninfo.ip.ip[0] &&
          strcmp(ninfo.ip.ip, "0.0.0.0") != 0) {
        snprintf(wlan_str, sizeof(wlan_str), "%s", LV_SYMBOL_WIFI);
      } else if (ninfo.flags & EOS_NETIF_FLAG_UP) {
        snprintf(wlan_str, sizeof(wlan_str), "%s..", LV_SYMBOL_WIFI);
      } else {
        snprintf(wlan_str, sizeof(wlan_str), "%s--", LV_SYMBOL_WIFI);
      }
    }
    close(wfd);
  } else {
    int afd = open("/dev/ap0", O_RDONLY);
    if (afd >= 0) {
      snprintf(wlan_str, sizeof(wlan_str), "%s AP", LV_SYMBOL_WIFI);
      close(afd);
    } else {
      snprintf(wlan_str, sizeof(wlan_str), "%s--", LV_SYMBOL_WIFI);
    }
  }

  // Line 1: WLAN icon, Battery icon/level, Uptime, Task list icon (Compact & centered)
  snprintf(out->line1, sizeof(out->line1),
           "%s  %s  %02u:%02u:%02u  " LV_SYMBOL_LIST " %u",
           wlan_str, bat_str, up_h, up_m, up_s, task_count);

  // Line 2: Drive/RAM icon with breakdown, Refresh/Load icon (No redundant words)
  snprintf(out->line2, sizeof(out->line2),
           LV_SYMBOL_DRIVE " %luK [I:%luK S:%luK]  " LV_SYMBOL_REFRESH " %.2f",
           (unsigned long)mem_free_kb,
           (unsigned long)mem_int_kb,
           (unsigned long)mem_spi_kb,
           load1);
}

int statusbar_main(int argc, char **argv) {
  bool auto_mode = false;
  int timeout_ms = -1;
  bar_parse_args(argc, argv, &auto_mode, &timeout_ms);

  const char *fb_path = getenv("EOS_FB");
  if (!fb_path || !*fb_path) {
    fb_path = "/dev/vfb1";
  }

  int fd = open(fb_path, O_RDWR);
  if (fd < 0) {
    fb_path = "/dev/fb0";
    fd = open(fb_path, O_RDWR);
  }
  if (fd < 0) {
    fprintf(stderr, "statusbar: cannot open %s: %s\n", fb_path, strerror(errno));
    return 1;
  }

  uint32_t width = 0, height = 0;
  bar_query_display_size(fd, &width, &height, 280, 32);

  size_t buf_pixels = (size_t)width * height;
  uint16_t *buf = (uint16_t *)malloc(buf_pixels * sizeof(uint16_t));
  if (!buf) {
    close(fd);
    return 1;
  }

  sbar_content_t last_content = {0};
  int elapsed_ms = 0;

  while (1) {
    sbar_content_t cur_content = {0};
    if (!read_sbar_file(&cur_content)) {
      read_fallback_stats(&cur_content);
    }

    if (strcmp(cur_content.line1, last_content.line1) != 0 ||
        strcmp(cur_content.line2, last_content.line2) != 0) {
      // Clear background to dark slate navy (0x10A2)
      for (size_t i = 0; i < buf_pixels; i++) {
        buf[i] = 0x10A2;
      }

      if (height >= 26) {
        // Line 1: High-contrast cyan text (0x38DF) - Centered
        int32_t base1 = (height >= 40) ? 18 : 13;
        uint32_t w1 = lvfont_measure_text(&lv_font_montserrat_14, cur_content.line1);
        int32_t x1 = (int32_t)(width > w1 ? (width - w1) / 2 : 2);
        lvfont_render_text(buf, width, height, &lv_font_montserrat_14,
                           cur_content.line1, 0x38DF, x1, base1);

        // Subtle divider rule at y = height / 2 - 1
        uint32_t div_y = (height / 2) - 1;
        for (uint32_t x = 4; x < width - 4; x++) {
          buf[div_y * width + x] = 0x2124;
        }

        // Line 2: Bright silver-white text (0xE71C) - Centered
        const char *l2 = cur_content.line2[0] ? cur_content.line2 : cur_content.line1;
        int32_t base2 = (height >= 40) ? (int32_t)(div_y + 20) : (int32_t)(div_y + 14);
        uint32_t w2 = lvfont_measure_text(&lv_font_montserrat_14, l2);
        int32_t x2 = (int32_t)(width > w2 ? (width - w2) / 2 : 2);
        lvfont_render_text(buf, width, height, &lv_font_montserrat_14,
                           l2, 0xE71C, x2, base2);
      } else {
        // Single-line compact mode - Centered
        int32_t base = (int32_t)height - 4;
        uint32_t w1 = lvfont_measure_text(&lv_font_montserrat_14, cur_content.line1);
        int32_t x1 = (int32_t)(width > w1 ? (width - w1) / 2 : 2);
        lvfont_render_text(buf, width, height, &lv_font_montserrat_14,
                           cur_content.line1, 0x38DF, x1, base);
      }

      eos_display_rect_t rect = {.x = 0, .y = 0, .w = width, .h = height};
      if (ioctl(fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect) != 0) {
        fprintf(stderr, "statusbar: SET_WINDOW failed: %s\n", strerror(errno));
        break;
      }
      if (write(fd, buf, buf_pixels * sizeof(uint16_t)) < 0) {
        fprintf(stderr, "statusbar: write failed: %s\n", strerror(errno));
        break;
      }
      ioctl(fd, EOS_DISPLAY_IOCTL_FLUSH);
      last_content = cur_content;
    }

    if (auto_mode) {
      break;
    }

    if (timeout_ms >= 0 && elapsed_ms >= timeout_ms) {
      break;
    }

    usleep(1000000);
    elapsed_ms += 1000;
  }

  free(buf);
  close(fd);
  return 0;
}

EOS_BIN_ATTR const eos_bin_t statusbar_app = {
    EOS_BIN_INITIALIZER,
    .filename = "statusbar",
    .name = "System Status Bar",
    .group = "System",
    .description = "Native status bar displaying system metrics and /tmp/sbar",
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_STACK_PSRAM | EOS_BIN_FLAG_ROLE_STATUS,
    .entry_point = statusbar_main};
