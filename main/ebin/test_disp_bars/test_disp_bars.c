#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"

static void draw_bars(int fd) {
  int w = 0, h = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) != 0 || w <= 0)
    return;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) != 0 || h <= 0)
    return;

  size_t frame_bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
#ifdef MALLOC_CAP_SPIRAM
  uint16_t *buf = heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  uint16_t *buf = NULL;
#endif
  if (!buf) {
    buf = malloc(frame_bytes);
  }
  if (!buf) {
    perror("test_disp_bars: malloc frame buffer");
    return;
  }

  // RGB565 color bars
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint16_t c;

      if (x < w / 6)
        c = 0xF800; // red
      else if (x < w * 2 / 6)
        c = 0x07E0; // green
      else if (x < w * 3 / 6)
        c = 0x001F; // blue
      else if (x < w * 4 / 6)
        c = 0xFFE0; // yellow
      else if (x < w * 5 / 6)
        c = 0xF81F; // magenta
      else
        c = 0x07FF; // cyan

      // 1-pixel white border
      if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
        c = 0xFFFF;

      // White diagonals
      if (x == y || x == (w - 1 - y))
        c = 0xFFFF;

      buf[y * w + x] = c;
    }
  }

  // Write full frame directly to framebuffer device
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_bars: write display");
  }

  // Keep frame visible briefly before exiting
  vTaskDelay(pdMS_TO_TICKS(2000));

  free(buf);
}

int test_disp_bars_main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "/dev/fb0";

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    perror("test_disp_bars: open display device");
    return -1;
  }

  draw_bars(fd);

  close(fd);

  return 0;
}

EOS_BIN_ATTR eos_bin_t test_disp_bars = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_bars",
    .name = "Display Bars",
    .group = "Tests",
    .description = "Render standard RGB565 SMPTE color bars and alignment borders on screen",
    .entry_point = test_disp_bars_main};
