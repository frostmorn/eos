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

static void fill_solid_frame(uint8_t *buf, int total_pixels, uint32_t fmt,
                             uint8_t r, uint8_t g, uint8_t b) {
  if (fmt == EOS_DISPLAY_FMT_RGB888) {
    for (int i = 0; i < total_pixels; i++) {
      buf[i * 3 + 0] = r;
      buf[i * 3 + 1] = g;
      buf[i * 3 + 2] = b;
    }
  } else {
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    uint16_t *buf16 = (uint16_t *)buf;
    for (int i = 0; i < total_pixels; i++) {
      buf16[i] = c;
    }
  }
}

static void fill_gradient_frame(uint8_t *buf, int w, int h, uint32_t fmt) {
  if (fmt == EOS_DISPLAY_FMT_RGB888) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t idx = ((size_t)y * w + x) * 3;
        buf[idx + 0] = (uint8_t)(x * EOS_COLOR_8BIT_MAX / w);
        buf[idx + 1] = (uint8_t)(y * EOS_COLOR_8BIT_MAX / h);
        buf[idx + 2] = 0;
      }
    }
  } else {
    uint16_t *buf16 = (uint16_t *)buf;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        buf16[y * w + x] = ((x * EOS_RGB565_RED_MAX / w) << 11) | ((y * EOS_RGB565_GREEN_MAX / h) << 5);
      }
    }
  }
}

static void test_colors(int fd, uint32_t fmt) {
  int w = 0, h = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) != 0 ||
      ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) != 0 || w <= 0 || h <= 0)
    return;

  size_t bpp = eos_display_format_bytes_per_pixel(fmt);
  size_t frame_bytes = (size_t)w * (size_t)h * bpp;
  uint8_t *buf = (uint8_t *)eos_psram_malloc_prefer(frame_bytes);
  if (!buf) {
    perror("test_disp_colors: malloc frame buffer");
    return;
  }

  // Red
  fill_solid_frame(buf, w * h, fmt, EOS_COLOR_8BIT_MAX, 0, 0);
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Red");
  }
  usleep(EOS_TIMEOUT_500_MS * EOS_US_PER_MS);

  // Green
  fill_solid_frame(buf, w * h, fmt, 0, EOS_COLOR_8BIT_MAX, 0);
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Green");
  }
  usleep(EOS_TIMEOUT_500_MS * EOS_US_PER_MS);

  // Blue
  fill_solid_frame(buf, w * h, fmt, 0, 0, EOS_COLOR_8BIT_MAX);
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Blue");
  }
  usleep(EOS_TIMEOUT_500_MS * EOS_US_PER_MS);

  // Gradient
  fill_gradient_frame(buf, w, h, fmt);
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Gradient");
  }
  usleep(EOS_US_PER_SEC);

  free(buf);
}

int test_disp_colors_main(int argc, char **argv) {
  eos_disp_test_args_t args = eos_disp_parse_args(argc, argv);
  uint32_t active_fmt = EOS_DISPLAY_FMT_RGB565;
  uint32_t orig_fmt = EOS_DISPLAY_FMT_RGB565;
  int fd = eos_disp_open_and_configure(&args, &active_fmt, &orig_fmt);
  if (fd < 0) {
    perror("test_disp_colors: open display device");
    return -1;
  }

  test_colors(fd, active_fmt);

  eos_disp_close_and_restore(fd, orig_fmt, active_fmt);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_disp_colors = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_ROLE_FULL,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_colors",
    .name = "Display Colors",
    .group = "Tests",
    .description = "Cycle primary and secondary solid colors across screen in RGB565 / RGB888",
    .entry_point = test_disp_colors_main};
