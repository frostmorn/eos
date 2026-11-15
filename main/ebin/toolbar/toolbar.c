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

#define TBAR_MAX_LINE 128

typedef struct {
  char line1[TBAR_MAX_LINE];
  char line2[TBAR_MAX_LINE];
} tbar_content_t;

static bool read_tbar_file(tbar_content_t *out) {
  char buf[TBAR_MAX_LINE * 2] = {0};
  if (bar_read_file("/tmp/tbar", buf, sizeof(buf)) <= 0) {
    return false;
  }

  char *line1 = NULL;
  char *line2 = NULL;
  bar_split_lines(buf, &line1, &line2);

  const char *text = NULL;
  if (!bar_validate_pid_prefix(line1, &text)) {
    return false;
  }

  // If a single line contains " | ", split into context title (line 1) and shortcuts (line 2)
  if (!line2 || line2[0] == '\0') {
    char *pipe_sep = strstr(text, " | ");
    if (pipe_sep) {
      size_t t1_len = (size_t)(pipe_sep - text);
      if (t1_len >= sizeof(out->line1)) t1_len = sizeof(out->line1) - 1;
      memcpy(out->line1, text, t1_len);
      out->line1[t1_len] = '\0';
      snprintf(out->line2, sizeof(out->line2), "%.*s", (int)(sizeof(out->line2) - 1), pipe_sep + 3);
      return true;
    }
  }

  snprintf(out->line1, sizeof(out->line1), "%.*s", (int)(sizeof(out->line1) - 1), text);
  if (line2 && line2[0] != '\0') {
    snprintf(out->line2, sizeof(out->line2), "%.*s", (int)(sizeof(out->line2) - 1), line2);
  } else {
    snprintf(out->line2, sizeof(out->line2), "[▲▼]Nav  [◄►]Tab  [A]Run  [D]Info");
  }
  return true;
}

int toolbar_main(int argc, char **argv) {
  bool auto_mode = false;
  int timeout_ms = -1;
  bar_parse_args(argc, argv, &auto_mode, &timeout_ms);

  const char *fb_path = getenv("EOS_FB");
  if (!fb_path || !*fb_path) {
    fb_path = "/dev/vfb2";
  }

  int fd = open(fb_path, O_RDWR);
  if (fd < 0) {
    fb_path = "/dev/fb0";
    fd = open(fb_path, O_RDWR);
  }
  if (fd < 0) {
    fprintf(stderr, "toolbar: cannot open %s: %s\n", fb_path, strerror(errno));
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

  tbar_content_t last_content = {0};
  int elapsed_ms = 0;

  while (1) {
    tbar_content_t cur_content = {0};
    if (!read_tbar_file(&cur_content)) {
      snprintf(cur_content.line1, sizeof(cur_content.line1), "Launcher");
      snprintf(cur_content.line2, sizeof(cur_content.line2), "[▲▼]Nav  [◄►]Tab  [A]Run  [D]Info");
    }

    if (strcmp(cur_content.line1, last_content.line1) != 0 ||
        strcmp(cur_content.line2, last_content.line2) != 0) {
      // Clear background to charcoal navy (0x18C3)
      for (size_t i = 0; i < buf_pixels; i++) {
        buf[i] = 0x18C3;
      }

      if (height >= 26) {
        // Line 1: Golden amber yellow text (0xFFE0) - Centered
        int32_t base1 = (height >= 40) ? 18 : 13;
        uint32_t w1 = lvfont_measure_text(&lv_font_montserrat_14, cur_content.line1);
        int32_t x1 = (int32_t)(width > w1 ? (width - w1) / 2 : 2);
        lvfont_render_text(buf, width, height, &lv_font_montserrat_14,
                           cur_content.line1, 0xFFE0, x1, base1);

        // Divider rule at y = height / 2 - 1
        uint32_t div_y = (height / 2) - 1;
        for (uint32_t x = 4; x < width - 4; x++) {
          buf[div_y * width + x] = 0x2945;
        }

        // Line 2: Emerald green action hints (0x2FE4) - Centered
        int32_t base2 = (height >= 40) ? (int32_t)(div_y + 20) : (int32_t)(div_y + 14);
        uint32_t w2 = lvfont_measure_text(&lv_font_montserrat_14, cur_content.line2);
        int32_t x2 = (int32_t)(width > w2 ? (width - w2) / 2 : 2);
        lvfont_render_text(buf, width, height, &lv_font_montserrat_14,
                           cur_content.line2, 0x2FE4, x2, base2);
      } else {
        // Single-line centered mode
        int32_t base = (int32_t)height - 4;
        uint32_t w1 = lvfont_measure_text(&lv_font_montserrat_14, cur_content.line1);
        int32_t x1 = (int32_t)(width > w1 ? (width - w1) / 2 : 2);
        lvfont_render_text(buf, width, height, &lv_font_montserrat_14,
                           cur_content.line1, 0xFFE0, x1, base);
      }

      eos_display_rect_t rect = {.x = 0, .y = 0, .w = width, .h = height};
      if (ioctl(fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect) != 0) {
        fprintf(stderr, "toolbar: SET_WINDOW failed: %s\n", strerror(errno));
        break;
      }
      if (write(fd, buf, buf_pixels * sizeof(uint16_t)) < 0) {
        fprintf(stderr, "toolbar: write failed: %s\n", strerror(errno));
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

    usleep(250000);
    elapsed_ms += 250;
  }

  free(buf);
  close(fd);
  return 0;
}

EOS_BIN_ATTR const eos_bin_t toolbar_app = {
    EOS_BIN_INITIALIZER,
    .filename = "toolbar",
    .name = "System Toolbar",
    .group = "System",
    .description = "Native bottom toolbar displaying context navigation and /tmp/tbar",
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_STACK_PSRAM | EOS_BIN_FLAG_ROLE_TOOLBAR,
    .entry_point = toolbar_main};
