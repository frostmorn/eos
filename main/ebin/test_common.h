#pragma once

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "ecore/display.h"
#include "ecore/ioctl.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

typedef struct {
  int passed;
  int failed;
} test_ctx_t;

typedef struct {
  const char *fb_path;
  uint32_t target_fmt;
} eos_disp_test_args_t;

static inline eos_disp_test_args_t eos_disp_parse_args(int argc, char **argv) {
  eos_disp_test_args_t args = {
      .fb_path = NULL,
      .target_fmt = EOS_DISPLAY_FMT_UNSPECIFIED,
  };
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--rgb888") == 0 || strcmp(argv[i], "-8") == 0) {
      args.target_fmt = EOS_DISPLAY_FMT_RGB888;
    } else if (strcmp(argv[i], "--rgb565") == 0 || strcmp(argv[i], "-5") == 0) {
      args.target_fmt = EOS_DISPLAY_FMT_RGB565;
    } else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "-d") == 0) &&
               i + 1 < argc) {
      i++;
    } else if (argv[i][0] != '-') {
      args.fb_path = argv[i];
    }
  }
  return args;
}

static inline int eos_disp_open_and_configure(const eos_disp_test_args_t *args, uint32_t *out_fmt, uint32_t *out_orig_fmt) {
  const char *path = args->fb_path;
  if (!path || !path[0]) {
    path = eos_display_default_fb_path();
  }
  int fd = open(path, O_RDWR);
  if (fd < 0) {
    return -1;
  }
  uint32_t orig_fmt = EOS_DISPLAY_FMT_RGB565;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMAT, &orig_fmt) != 0) {
    orig_fmt = EOS_DISPLAY_FMT_RGB565;
  }
  if (out_orig_fmt) {
    *out_orig_fmt = orig_fmt;
  }
  if (args->target_fmt != EOS_DISPLAY_FMT_UNSPECIFIED) {
    (void)ioctl(fd, EOS_DISPLAY_IOCTL_SET_FORMAT, args->target_fmt);
  }
  uint32_t fmt = orig_fmt;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMAT, &fmt) != 0) {
    fmt = orig_fmt;
  }
  if (out_fmt) {
    *out_fmt = fmt;
  }
  return fd;
}

static inline void eos_disp_close_and_restore(int fd, uint32_t orig_fmt, uint32_t active_fmt) {
  if (fd >= 0) {
    if (orig_fmt != EOS_DISPLAY_FMT_UNSPECIFIED && active_fmt != orig_fmt) {
      (void)ioctl(fd, EOS_DISPLAY_IOCTL_SET_FORMAT, orig_fmt);
    }
    close(fd);
  }
}

#define DISP_GRADIENT_TIME 3
#define TEST_DATA_PATH     "/tmp/h_test.dat"
#define TEST_LIST_PATH     "/tmp/h_test.lst"
#define CHURN_THREADS      4
#define CHURN_ITERS        25
#define TEST_PROC_STRESS_CYCLES 1000
#define TEST_BIN_DEFAULT_STACK_SIZE 16384
#define TEST_HASH_PATTERN_LEN 1000
#define TEST_HEX_STR_LEN      EOS_NUM_STR_LEN

static inline void report_test(test_ctx_t *ctx, const char *name, bool ok) {
  if (ok) {
    printf("  [PASS] %s\n", name);
    ctx->passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    ctx->failed++;
  }
}
