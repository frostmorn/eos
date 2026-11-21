#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
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

static void fire_palette_rgb(uint8_t heat, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (heat < 85) {
    *r = heat * 3;
    *g = 0;
    *b = 0;
  } else if (heat < 170) {
    *r = EOS_COLOR_8BIT_MAX;
    *g = (heat - 85) * 3;
    *b = 0;
  } else {
    *r = EOS_COLOR_8BIT_MAX;
    *g = EOS_COLOR_8BIT_MAX;
    *b = (heat - 170) * 3;
  }
}

static void test_doom_fire(int fd, uint32_t fmt, int max_frames) {
  int w = 0, h = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) != 0 || w <= 0)
    return;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) != 0 || h <= 0)
    return;

  size_t fire_bytes = (size_t)w * (size_t)h;
  size_t bpp = eos_display_format_bytes_per_pixel(fmt);
  size_t frame_bytes = fire_bytes * bpp;

  uint8_t *fire = (uint8_t *)eos_psram_malloc_prefer(fire_bytes);
  uint8_t *buf = (uint8_t *)eos_psram_malloc_prefer(frame_bytes);

  if (!fire || !buf) {
    free(fire);
    free(buf);
    return;
  }

  memset(fire, 0, fire_bytes);

  // ignite bottom
  for (int x = 0; x < w; x++)
    fire[(h - 1) * w + x] = EOS_COLOR_8BIT_MAX;

  int frame = 0;
  while (max_frames <= 0 || frame < max_frames) {
    // propagate fire upward
    for (int y = 0; y < h - 1; y++) {
      for (int x = 0; x < w; x++) {
        int src = (y + 1) * w + x;
        int decay = rand() & 3;
        int dst_x = x;

        if (decay && dst_x > 0)
          dst_x--;

        int value = fire[src] - decay;
        if (value < 0)
          value = 0;

        fire[y * w + dst_x] = value;
      }
    }

    // keep bottom hot
    for (int x = 0; x < w; x++)
      fire[(h - 1) * w + x] = EOS_COLOR_8BIT_MAX;

    // convert heat -> pixel buffer
    if (fmt == EOS_DISPLAY_FMT_RGB888) {
      for (int i = 0; i < w * h; i++) {
        uint8_t r = 0, g = 0, b = 0;
        fire_palette_rgb(fire[i], &r, &g, &b);
        buf[i * 3 + 0] = r;
        buf[i * 3 + 1] = g;
        buf[i * 3 + 2] = b;
      }
    } else {
      uint16_t *buf16 = (uint16_t *)buf;
      for (int i = 0; i < w * h; i++) {
        uint8_t r = 0, g = 0, b = 0;
        fire_palette_rgb(fire[i], &r, &g, &b);
        buf16[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      }
    }

    if (write(fd, buf, frame_bytes) < 0)
      break;

    frame++;
    usleep(30 * EOS_US_PER_MS);
  }

  free(fire);
  free(buf);
}

static int test_disp_doom_fire_main(int argc, char **argv) {
  eos_disp_test_args_t args = eos_disp_parse_args(argc, argv);
  int max_frames = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      max_frames = atoi(argv[++i]);
    }
  }

  uint32_t active_fmt = EOS_DISPLAY_FMT_RGB565;
  uint32_t orig_fmt = EOS_DISPLAY_FMT_RGB565;
  int fd = eos_disp_open_and_configure(&args, &active_fmt, &orig_fmt);
  if (fd < 0) {
    perror("test_disp_doom_fire: open display device");
    return -1;
  }

  test_doom_fire(fd, active_fmt, max_frames);

  eos_disp_close_and_restore(fd, orig_fmt, active_fmt);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_disp_doom_fire = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_ROLE_FULL,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_doom_fire",
    .name = "Doom Fire",
    .group = "Tests",
    .description = "Classic Doom procedural fire animation in RGB565 / RGB888",
    .entry_point = test_disp_doom_fire_main};
