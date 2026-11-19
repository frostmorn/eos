#include "umount.h"
#include "ecore/bin.h"
#include "ecore/fs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void umount_print_usage(void) {
  printf("Usage: umount <target | device>\n");
}

int umount_main(int argc, char **argv) {
  if (argc < 2) {
    umount_print_usage();
    return 1;
  }

  const char *target_or_dev = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      umount_print_usage();
      return 0;
    }
    if (!target_or_dev) {
      target_or_dev = argv[i];
    } else {
      umount_print_usage();
      return 1;
    }
  }

  if (!target_or_dev) {
    umount_print_usage();
    return 1;
  }

  int rc = eos_umount(target_or_dev, 0);
  if (rc != 0) {
    fprintf(stderr, "umount: %s: %s\n", target_or_dev, strerror(errno));
    return 1;
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t umount_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "umount",
    .name = "Unmount Filesystem",
    .group = "System",
    .description = "Unmount a currently mounted storage device or filesystem",
    .entry_point = umount_main};
