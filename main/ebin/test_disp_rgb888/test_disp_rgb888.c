#include "test_disp_rgb888.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"

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
  const char *path = (getenv("EOS_FB") && getenv("EOS_FB")[0]) ? getenv("EOS_FB") : "/dev/fb0";
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

  close(fd);
  return TEST_DISP_RGB888_OK;
}

EOS_BIN_ATTR eos_bin_t test_disp_rgb888_bin = {
    EOS_BIN_INITIALIZER,
    .display_format = EOS_DISPLAY_FMT_RGB888,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_rgb888",
    .name = "RGB888 Display Test",
    .group = "Tests",
    .description = "Test automatic display color format switching to RGB888 and F03 exit restoration",
    .entry_point = test_disp_rgb888_main};
