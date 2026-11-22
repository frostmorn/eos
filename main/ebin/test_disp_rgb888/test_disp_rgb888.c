#include "test_disp_rgb888.h"
#include "ecore/bin.h"
#include "ecore/display.h"
#include "ecore/ioctl.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int test_disp_rgb888_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  const char *path = eos_display_default_fb_path();
  int fd = open(path, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "test_disp_rgb888: cannot open '%s': %s\n", path, strerror(errno));
    return TEST_DISP_RGB888_ERR;
  }

  uint32_t active_fmt = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMAT, &active_fmt) == 0) {
    printf("test_disp_rgb888: running with active format %lu on '%s'\n",
           (unsigned long)active_fmt, path);
  }
  fflush(stdout);

  if (active_fmt == EOS_DISPLAY_FMT_RGB888) {
    int w = 0, h = 0;
    if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) == 0 &&
        ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) == 0 && w > 0 && h > 0) {
      size_t frame_bytes = (size_t)w * (size_t)h * 3;
      uint8_t *buf = (uint8_t *)eos_psram_malloc_prefer(frame_bytes);
      if (buf) {
        for (int y = 0; y < h; y++) {
          for (int x = 0; x < w; x++) {
            size_t idx = ((size_t)y * w + x) * 3;
            buf[idx + 0] = (uint8_t)(x * EOS_COLOR_8BIT_MAX / w);
            buf[idx + 1] = (uint8_t)(y * EOS_COLOR_8BIT_MAX / h);
            buf[idx + 2] = EOS_COLOR_8BIT_MAX / 2;
          }
        }
        (void)write(fd, buf, frame_bytes);
        free(buf);
      }
    }
  }

  close(fd);
  return TEST_DISP_RGB888_OK;
}

EOS_BIN_ATTR eos_bin_t test_disp_rgb888_bin = {
    EOS_BIN_INITIALIZER,
    .display_format = EOS_DISPLAY_FMT_RGB888,
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_ROLE_FULL,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_rgb888",
    .name = "RGB888 Display Test",
    .description = "Test automatic display color format switching to RGB888 and F03 exit restoration",
    .group = "Tests",
    .entry_point = test_disp_rgb888_main};
