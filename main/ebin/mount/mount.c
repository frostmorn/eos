#include "mount.h"
#include "ecore/bin.h"
#include "ecore/fs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mount_print_table(void) {
  FILE *f = fopen("/proc/mounts", "r");
  if (!f) {
    fprintf(stderr, "mount: cannot open /proc/mounts: %s\n", strerror(errno));
    return 1;
  }
  char line[MOUNT_LINE_BUF_SIZE];
  while (fgets(line, sizeof(line), f)) {
    fputs(line, stdout);
  }
  fclose(f);
  return 0;
}

static void mount_print_usage(void) {
  printf("Usage: mount [-t <type>] [-o <options>] [<source> <target>]\n");
}

int mount_main(int argc, char **argv) {
  if (argc == 1) {
    return mount_print_table();
  }

  const char *fstype = NULL;
  const char *opts = NULL;
  const char *source = NULL;
  const char *target = NULL;
  uint32_t flags = EOS_MOUNT_FLAG_NONE;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      mount_print_usage();
      return 0;
    }
    if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      fstype = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      opts = argv[++i];
      if (strstr(opts, "ro")) {
        flags |= EOS_MOUNT_FLAG_RDONLY;
      }
      continue;
    }
    if (!source) {
      source = argv[i];
    } else if (!target) {
      target = argv[i];
    } else {
      mount_print_usage();
      return 1;
    }
  }

  if (!source || !target) {
    mount_print_usage();
    return 1;
  }

  int rc = eos_mount(source, target, fstype, flags, opts);
  if (rc != 0) {
    fprintf(stderr, "mount: %s: %s\n", target, strerror(errno));
    return 1;
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t mount_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "mount",
    .name = "Mount Filesystem",
    .group = "System",
    .description = "Mount storage devices or virtual filesystems to target directory paths",
    .entry_point = mount_main};
