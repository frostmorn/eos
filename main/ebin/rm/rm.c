#include "ecore/bin.h"
#include "ebin/fs_common.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int lstat(const char *path, struct stat *st);

static void print_usage(void) {
  printf("Usage: rm [-r|-R] [-f] [-v] FILE...\n"
         "Remove (unlink) the FILE(s).\n\n"
         "Options:\n"
         "  -f, --force          Ignore nonexistent files, never prompt\n"
         "  -r, -R, --recursive  Remove directories and their contents recursively\n"
         "  -v, --verbose        Explain what is being done\n"
         "  -h, --help           Display this help and exit\n");
}

static int remove_recursive(const char *path, bool force, bool verbose) {
  struct stat st;
  if (lstat(path, &st) != 0) {
    if (force && errno == ENOENT) {
      return 0;
    }
    printf("rm: cannot remove '%s': %s\n", path, strerror(errno));
    return -1;
  }

  if (!S_ISDIR(st.st_mode)) {
    if (unlink(path) != 0) {
      if (force && errno == ENOENT) {
        return 0;
      }
      printf("rm: cannot remove '%s': %s\n", path, strerror(errno));
      return -1;
    }
    if (verbose) {
      printf("removed '%s'\n", path);
    }
    return 0;
  }

  DIR *dir = opendir(path);
  if (!dir) {
    if (force && errno == ENOENT) {
      return 0;
    }
    printf("rm: cannot open directory '%s': %s\n", path, strerror(errno));
    return -1;
  }

  int ret = 0;
  struct dirent *entry;
  char child_path[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    size_t path_len = strlen(path);
    if (path_len > 0 && path[path_len - 1] == '/') {
      snprintf(child_path, sizeof(child_path), "%s%s", path, entry->d_name);
    } else {
      snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
    }

    if (remove_recursive(child_path, force, verbose) != 0) {
      ret = -1;
    }
  }

  closedir(dir);

  if (rmdir(path) != 0) {
    if (!force || errno != ENOENT) {
      printf("rm: cannot remove directory '%s': %s\n", path, strerror(errno));
      ret = -1;
    }
  } else if (verbose) {
    printf("removed directory '%s'\n", path);
  }

  return ret;
}

int rm_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return -1;
  }

  bool force = false;
  bool recursive = false;
  bool verbose = false;
  int first_file = 0;
  fs_opts_config_t opt_cfg = {.tool_name = "rm", .print_help = print_usage, .allow_recursive = true};
  if (fs_parse_options(argc, argv, &opt_cfg, &force, &recursive, &verbose, &first_file) != 0) {
    return -1;
  }
  if (first_file == -2) {
    return 0;
  }

  if (first_file == 0 || first_file >= argc) {
    if (!force) {
      printf("rm: missing operand\n");
      return -1;
    }
    return 0;
  }

  int ret = 0;
  for (int i = first_file; i < argc; i++) {
    const char *target = argv[i];
    struct stat st;

    if (stat(target, &st) != 0) {
      if (force && errno == ENOENT) {
        continue;
      }
      printf("rm: cannot remove '%s': %s\n", target, strerror(errno));
      ret = -1;
      continue;
    }

    if (S_ISDIR(st.st_mode)) {
      if (!recursive) {
        printf("rm: cannot remove '%s': Is a directory\n", target);
        ret = -1;
        continue;
      }
      if (remove_recursive(target, force, verbose) != 0) {
        ret = -1;
      }
    } else {
      if (unlink(target) != 0) {
        if (!force || errno != ENOENT) {
          printf("rm: cannot remove '%s': %s\n", target, strerror(errno));
          ret = -1;
        }
      } else if (verbose) {
        printf("removed '%s'\n", target);
      }
    }
  }

  return ret;
}

EOS_BIN_ATTR eos_bin_t rm_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "rm",
    .name = "Remove File",
    .group = "Utils",
    .description = "Remove files or directories",
    .entry_point = rm_main,
};
