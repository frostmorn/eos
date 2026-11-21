#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <esp_heap_caps.h>

#include "ebin/test_common.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"

static void get_bar_rgb(int x, int y, int w, int h, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (x < w / 6) {
    *r = EOS_COLOR_8BIT_MAX;
    *g = 0;
    *b = 0;
  } else if (x < w * 2 / 6) {
    *r = 0;
    *g = EOS_COLOR_8BIT_MAX;
    *b = 0;
  } else if (x < w * 3 / 6) {
    *r = 0;
    *g = 0;
    *b = EOS_COLOR_8BIT_MAX;
  } else if (x < w * 4 / 6) {
    *r = EOS_COLOR_8BIT_MAX;
    *g = EOS_COLOR_8BIT_MAX;
    *b = 0;
  } else if (x < w * 5 / 6) {
    *r = EOS_COLOR_8BIT_MAX;
    *g = 0;
    *b = EOS_COLOR_8BIT_MAX;
  } else {
    *r = 0;
    *g = EOS_COLOR_8BIT_MAX;
    *b = EOS_COLOR_8BIT_MAX;
  }

  bool on_diag = false;
  if (w > 1 && h > 1) {
    int64_t d1 = (int64_t)x * (h - 1) / (w - 1);
    int64_t d2 = (int64_t)(w - 1 - x) * (h - 1) / (w - 1);
    if (y == d1 || y == d2) {
      on_diag = true;
    }
  }

  if (x == 0 || y == 0 || x == w - 1 || y == h - 1 || on_diag) {
    *r = EOS_COLOR_8BIT_MAX;
    *g = EOS_COLOR_8BIT_MAX;
    *b = EOS_COLOR_8BIT_MAX;
  }
}

static void draw_bars(int fd, uint32_t fmt) {
  int w = 0, h = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) != 0 || w <= 0)
    return;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) != 0 || h <= 0)
    return;

  size_t bpp = eos_display_format_bytes_per_pixel(fmt);
  size_t frame_bytes = (size_t)w * (size_t)h * bpp;
  uint8_t *buf = (uint8_t *)eos_psram_malloc_prefer(frame_bytes);
  if (!buf) {
    perror("test_disp_bars: malloc frame buffer");
    return;
  }

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t r = 0, g = 0, b = 0;
      get_bar_rgb(x, y, w, h, &r, &g, &b);

      if (fmt == EOS_DISPLAY_FMT_RGB888) {
        size_t idx = ((size_t)y * w + x) * 3;
        buf[idx + 0] = r;
        buf[idx + 1] = g;
        buf[idx + 2] = b;
      } else {
        uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        ((uint16_t *)buf)[y * w + x] = c;
      }
    }
  }

  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_bars: write display");
  }

  usleep(2 * EOS_US_PER_SEC);
  free(buf);
}

int test_disp_bars_main(int argc, char **argv) {
  eos_disp_test_args_t args = eos_disp_parse_args(argc, argv);
  uint32_t active_fmt = EOS_DISPLAY_FMT_RGB565;
  uint32_t orig_fmt = EOS_DISPLAY_FMT_RGB565;
  int fd = eos_disp_open_and_configure(&args, &active_fmt, &orig_fmt);
  if (fd < 0) {
    perror("test_disp_bars: open display device");
    return -1;
  }

  draw_bars(fd, active_fmt);

  eos_disp_close_and_restore(fd, orig_fmt, active_fmt);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_disp_bars = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_ROLE_FULL,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_bars",
    .name = "Display Bars",
    .group = "Tests",
    .description = "Render RGB565 / RGB888 SMPTE color bars and alignment borders on screen",
    .entry_point = test_disp_bars_main};
