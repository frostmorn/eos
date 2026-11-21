#include "test_disp_format.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int run_format_self_test(int fd, const char *path) {
  int passed = 0;
  int failed = 0;

  uint32_t width = 0;
  uint32_t height = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &width) == 0 &&
      ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &height) == 0 &&
      width > 0 && height > 0) {
    printf("  [PASS] Display dimensions: %lux%lu\n", (unsigned long)width, (unsigned long)height);
    passed++;
  } else {
    printf("  [FAIL] Failed to query display dimensions: %s\n", strerror(errno));
    failed++;
  }

  uint32_t supported = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMATS, &supported) == 0 && supported != 0) {
    printf("  [PASS] Supported formats mask: 0x%08lx\n", (unsigned long)supported);
    passed++;
  } else {
    printf("  [FAIL] GET_FORMATS failed: %s\n", strerror(errno));
    failed++;
  }

  uint32_t current_fmt = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMAT, &current_fmt) == 0) {
    printf("  [PASS] Current format: %lu\n", (unsigned long)current_fmt);
    fflush(stdout);
    passed++;
  } else {
    printf("  [FAIL] GET_FORMAT failed: %s\n", strerror(errno));
    fflush(stdout);
    failed++;
  }

  // Verify write byte alignment validation (write 1 byte when format is 16-bit or 24-bit)
  uint8_t odd_byte = 0xAA;
  ssize_t wr = write(fd, &odd_byte, 1);
  if (wr < 0 && errno == EINVAL) {
    printf("  [PASS] Odd byte write rejected with EINVAL as expected\n");
    passed++;
  } else {
    printf("  [FAIL] Odd byte write did not return EINVAL (wr=%ld, errno=%d)\n", (long)wr, errno);
    failed++;
  }

  // Attempt to set an unsupported format
  uint32_t invalid_fmt = EOS_DISPLAY_FMT_MONO1;
  if (!(supported & EOS_DISPLAY_FMT_MASK_MONO1)) {
    if (ioctl(fd, EOS_DISPLAY_IOCTL_SET_FORMAT, invalid_fmt) < 0 && errno == ENOTSUP) {
      printf("  [PASS] Unsupported format rejected with ENOTSUP as expected\n");
      passed++;
    } else {
      printf("  [FAIL] Unsupported format did not fail with ENOTSUP (errno=%d)\n", errno);
      failed++;
    }
  }

  // Verify setting active format succeeds
  if (ioctl(fd, EOS_DISPLAY_IOCTL_SET_FORMAT, current_fmt) == 0) {
    printf("  [PASS] SET_FORMAT to current format succeeded\n");
    passed++;
  } else {
    printf("  [FAIL] SET_FORMAT failed: %s\n", strerror(errno));
    failed++;
  }

  printf("\nDisplay Format Self-Test: %d passed, %d failed on '%s'\n", passed, failed, path);
  fflush(stdout);
  return (failed == 0) ? TEST_DISP_FMT_SUCCESS : TEST_DISP_FMT_FAILURE;
}

int test_disp_format_main(int argc, char **argv) {
  const char *path = (getenv("EOS_FB") && getenv("EOS_FB")[0]) ? getenv("EOS_FB") : "/dev/fb0";
  if (argc > 1 && argv[1][0] != '-') {
    path = argv[1];
  }

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "test_disp_format: cannot open '%s': %s\n", path, strerror(errno));
    return TEST_DISP_FMT_FAILURE;
  }

  int rc = run_format_self_test(fd, path);
  close(fd);
  return rc;
}

EOS_BIN_ATTR eos_bin_t test_disp_format_bin = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_ROLE_FULL,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_format",
    .name = "Display Format Test",
    .group = "Tests",
    .description = "Test display color format query, negotiation, and write validation",
    .entry_point = test_disp_format_main};
