#include "ebin/fs_common.h"
#include "ecore/bin.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MV_BUFFER_SIZE 1024

static void print_usage(void) {
  printf("Usage: mv [-f] [-v] SOURCE... DEST\n"
         "Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n\n"
         "Options:\n"
         "  -f, --force          Do not prompt before overwriting\n"
         "  -v, --verbose        Explain what is being done\n"
         "  -h, --help           Display this help and exit\n");
}

#define get_basename fs_get_basename
#define join_path fs_join_path
#define is_directory fs_is_directory
#define is_same_file fs_is_same_file

static int remove_path_recursive(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    return (errno == ENOENT) ? 0 : -1;
  }

  if (!S_ISDIR(st.st_mode)) {
    return unlink(path);
  }

  DIR *dir = opendir(path);
  if (!dir) {
    return -1;
  }

  int ret = 0;
  struct dirent *entry;
  char child_path[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    join_path(child_path, sizeof(child_path), path, entry->d_name);
    if (remove_path_recursive(child_path) != 0) {
      ret = -1;
    }
  }

  closedir(dir);
  if (rmdir(path) != 0) {
    ret = -1;
  }
  return ret;
}

static int fallback_copy_file(const char *src, const char *dst, bool force) {
  FILE *fsrc = fopen(src, "rb");
  if (!fsrc) {
    return -1;
  }

  if (force) {
    unlink(dst);
  }

  FILE *fdst = fopen(dst, "wb");
  if (!fdst && force) {
    unlink(dst);
    fdst = fopen(dst, "wb");
  }
  if (!fdst) {
    fclose(fsrc);
    return -1;
  }

  char buf[MV_BUFFER_SIZE];
  size_t n_read = 0;
  int ret = 0;

  while ((n_read = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
    if (fwrite(buf, 1, n_read, fdst) != n_read) {
      ret = -1;
      break;
    }
  }

  if (ferror(fsrc)) {
    ret = -1;
  }

  fclose(fsrc);
  fclose(fdst);
  return ret;
}

static int fallback_copy_dir(const char *src, const char *dst, bool force);

static int mv_fallback_walk_entry(const char *src, const char *dst, bool is_dir, void *ctx) {
  bool force = *(bool *)ctx;
  if (is_dir) {
    return fallback_copy_dir(src, dst, force);
  }
  return fallback_copy_file(src, dst, force);
}

static int fallback_copy_dir(const char *src, const char *dst, bool force) {
  if (mkdir(dst, 0755) != 0 && errno != EEXIST) {
    return -1;
  }

  DIR *dir = opendir(src);
  if (!dir) {
    return -1;
  }

  return fs_walk_dir_pairs(dir, src, dst, mv_fallback_walk_entry, &force);
}

static int move_entry(const char *src, const char *dst, bool force, bool verbose) {
  if (is_same_file(src, dst)) {
    printf("mv: '%s' and '%s' are the same file\n", src, dst);
    return -1;
  }

  struct stat st_src;
  if (stat(src, &st_src) != 0) {
    printf("mv: cannot stat '%s': %s\n", src, strerror(errno));
    return -1;
  }

  if (force && !is_directory(dst)) {
    unlink(dst);
  }

  // First, attempt atomic rename
  if (rename(src, dst) == 0) {
    if (verbose) {
      printf("renamed '%s' -> '%s'\n", src, dst);
    }
    return 0;
  }

  // If rename fails with EXDEV, fall back to copy + remove
  if (errno == EXDEV) {
    int cp_res = 0;
    if (S_ISDIR(st_src.st_mode)) {
      cp_res = fallback_copy_dir(src, dst, force);
    } else {
      cp_res = fallback_copy_file(src, dst, force);
    }

    if (cp_res == 0) {
      remove_path_recursive(src);
      if (verbose) {
        printf("renamed '%s' -> '%s'\n", src, dst);
      }
      return 0;
    }

    // Clean up destination on copy failure
    remove_path_recursive(dst);
    printf("mv: cannot move '%s' to '%s': copy failed\n", src, dst);
    return -1;
  }

  printf("mv: cannot move '%s' to '%s': %s\n", src, dst, strerror(errno));
  return -1;
}

int mv_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return -1;
  }

  bool force = false;
  bool verbose = false;
  int first_file = 0;
  fs_opts_config_t opt_cfg = {.tool_name = "mv", .print_help = print_usage, .allow_recursive = false};
  if (fs_parse_options(argc, argv, &opt_cfg, &force, NULL, &verbose, &first_file) != 0) {
    return -1;
  }
  if (first_file == -2) {
    return 0;
  }

  int num_sources = (argc - 1) - first_file;
  if (first_file == 0 || num_sources < 1 || argc < 3) {
    printf("mv: missing destination file operand\n");
    return -1;
  }

  const char *dest = argv[argc - 1];
  bool dest_is_dir = is_directory(dest);

  if (num_sources > 1 && !dest_is_dir) {
    printf("mv: target '%s' is not a directory\n", dest);
    return -1;
  }

  int ret = 0;
  for (int i = first_file; i < argc - 1; i++) {
    const char *src = argv[i];
    char target_path[PATH_MAX];

    if (dest_is_dir) {
      const char *base = get_basename(src);
      join_path(target_path, sizeof(target_path), dest, base);
    } else {
      strlcpy(target_path, dest, sizeof(target_path));
    }

    if (move_entry(src, target_path, force, verbose) != 0) {
      ret = -1;
    }
  }

  return ret;
}

EOS_BIN_ATTR eos_bin_t mv_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "mv",
    .name = "Move File",
    .group = "Utils",
    .description = "Move (rename) files or directories",
    .entry_point = mv_main,
};
