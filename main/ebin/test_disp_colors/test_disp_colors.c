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
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"

static void test_colors(int fd) {
  int w = 0, h = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) != 0 || ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) != 0 || w <= 0 || h <= 0)
    return;

  size_t frame_bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
  uint16_t *buf = (uint16_t *)eos_psram_malloc_prefer(frame_bytes);
  if (!buf) {
    perror("test_disp_colors: malloc frame buffer");
    return;
  }

  // Red
  for (int i = 0; i < w * h; i++)
    buf[i] = EOS_RGB565_RED_MASK;
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Red");
  }
  usleep(EOS_TIMEOUT_500_MS * EOS_US_PER_MS);

  // Green
  for (int i = 0; i < w * h; i++)
    buf[i] = EOS_RGB565_GREEN_MASK;
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Green");
  }
  usleep(EOS_TIMEOUT_500_MS * EOS_US_PER_MS);

  // Blue
  for (int i = 0; i < w * h; i++)
    buf[i] = EOS_RGB565_BLUE_MASK;
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Blue");
  }
  usleep(EOS_TIMEOUT_500_MS * EOS_US_PER_MS);

  // Gradient
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      buf[y * w + x] = ((x * EOS_RGB565_RED_MAX / w) << 11) | ((y * EOS_RGB565_GREEN_MAX / h) << 5);
  if (write(fd, buf, frame_bytes) < 0) {
    perror("test_disp_colors: write Gradient");
  }
  usleep(EOS_US_PER_SEC);

  free(buf);
}

int test_disp_colors_main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : (getenv("EOS_FB") ? getenv("EOS_FB") : (access("/dev/vfb0", F_OK) == 0 ? "/dev/vfb0" : "/dev/fb0"));

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
