#include "mount.h"
#include "ecore/bin.h"
#include "ecore/fs.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static bool is_partition_mounted(const char *dev_path) {
  FILE *f = fopen("/proc/mounts", "r");
  if (!f) return false;
  char line[MOUNT_LINE_BUF_SIZE];
  bool mounted = false;
  size_t dlen = strlen(dev_path);
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, dev_path, dlen) == 0 && line[dlen] == ' ') {
      mounted = true;
      break;
    }
  }
  fclose(f);
  return mounted;
}

static int mount_all_partitions(void) {
  DIR *d = opendir("/dev");
  if (!d) return 0;

  char part_names[MOUNT_MAX_PARTS][EOS_XSMALL_STR_LEN];
  int part_count = 0;

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (strncmp(de->d_name, "part", 4) == 0 && isdigit((unsigned char)de->d_name[4])) {
      if (part_count < MOUNT_MAX_PARTS) {
        strlcpy(part_names[part_count++], de->d_name, sizeof(part_names[0]));
      }
    }
  }
  closedir(d);

  if (part_count == 0) {
    return 0;
  }

  for (int i = 0; i < part_count - 1; i++) {
    for (int j = 0; j < part_count - i - 1; j++) {
      if (strcmp(part_names[j], part_names[j + 1]) > 0) {
        char tmp[EOS_XSMALL_STR_LEN];
        memcpy(tmp, part_names[j], sizeof(tmp));
        memcpy(part_names[j], part_names[j + 1], sizeof(tmp));
        memcpy(part_names[j + 1], tmp, sizeof(tmp));
      }
    }
  }

  (void)mkdir("/mnt", EOS_DEFAULT_DIR_MODE);

  const eos_fs_type_t *fat_fs = eos_fs_find("fat");
  const char *default_target = (fat_fs && fat_fs->default_target) ? fat_fs->default_target : "/mnt/sd";

  for (int i = 0; i < part_count; i++) {
    char dev_path[EOS_SMALL_STR_LEN];
    snprintf(dev_path, sizeof(dev_path), "/dev/%s", part_names[i]);

    if (is_partition_mounted(dev_path)) {
      continue;
    }

    char target[EOS_SMALL_STR_LEN];
    if (strcmp(part_names[i], "part0") == 0) {
      strlcpy(target, default_target, sizeof(target));
    } else {
      snprintf(target, sizeof(target), "%s%d", default_target, i);
    }

    (void)mkdir(target, EOS_DEFAULT_DIR_MODE);

    int rc = eos_mount(dev_path, target, "auto", EOS_MOUNT_FLAG_NONE, NULL);
    if (rc == 0) {
      printf("mount: mounted %s at %s\n", dev_path, target);
    }
  }

  return 0;
}

static void mount_print_usage(void) {
  printf("Usage: mount [-a|--all] [-t <type>] [-o <options>] [<source> <target>]\n");
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
    if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
      return mount_all_partitions();
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
