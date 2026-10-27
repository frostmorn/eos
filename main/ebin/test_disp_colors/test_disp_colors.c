#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <esp_heap_caps.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"

static void test_colors(int fd) {
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
    perror("test_disp_colors: malloc frame buffer");
    return;
  }

  // Red
  for (int i = 0; i < w * h; i++)
    buf[i] = 0xF800;
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Red");
  }
  usleep(500 * 1000);

  // Green
  for (int i = 0; i < w * h; i++)
    buf[i] = 0x07E0;
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Green");
  }
  usleep(500 * 1000);

  // Blue
  for (int i = 0; i < w * h; i++)
    buf[i] = 0x001F;
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Blue");
  }
  usleep(500 * 1000);

  // Gradient
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      buf[y * w + x] = ((x * 31 / w) << 11) | ((y * 63 / h) << 5);
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Gradient");
  }
  usleep(1000 * 1000);

  free(buf);
}

int test_disp_colors_main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "/dev/fb0";

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    perror("test_disp_colors: open display device");
    return -1;
  }

  test_colors(fd);

  close(fd);

  return 0;
}

EOS_BIN_ATTR eos_bin_t test_disp_colors = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_colors",
    .name = "Display Colors",
    .group = "Tests",
    .description = "Cycle primary and secondary solid colors across full screen display",
    .entry_point = test_disp_colors_main};
