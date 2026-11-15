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

#define CP_BUFFER_SIZE 1024

static void print_usage(void) {
  printf("Usage: cp [-r|-R] [-f] [-v] SOURCE... DEST\n"
         "Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n\n"
         "Options:\n"
         "  -f, --force          If an existing destination file cannot be opened,\n"
         "                       remove it and try again\n"
         "  -r, -R, --recursive  Copy directories recursively\n"
         "  -v, --verbose        Explain what is being done\n"
         "  -h, --help           Display this help and exit\n");
}

#define get_basename fs_get_basename
#define join_path fs_join_path
#define is_directory fs_is_directory
#define is_same_file fs_is_same_file

static int copy_single_file(const char *src, const char *dst, bool force, bool verbose) {
  if (is_same_file(src, dst)) {
    printf("cp: '%s' and '%s' are the same file\n", src, dst);
    return -1;
  }

  FILE *fsrc = fopen(src, "rb");
  if (!fsrc) {
    printf("cp: cannot open '%s': %s\n", src, strerror(errno));
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
    printf("cp: cannot create regular file '%s': %s\n", dst, strerror(errno));
    fclose(fsrc);
    return -1;
  }

  char *buf = malloc(CP_BUFFER_SIZE);
  if (!buf) {
    printf("cp: out of memory\n");
    fclose(fsrc);
    fclose(fdst);
    return -1;
  }
  size_t n_read = 0;
  int ret = 0;

  while ((n_read = fread(buf, 1, CP_BUFFER_SIZE, fsrc)) > 0) {
    size_t n_written = fwrite(buf, 1, n_read, fdst);
    if (n_written != n_read) {
      printf("cp: error writing to '%s': %s\n", dst, strerror(errno));
      ret = -1;
      break;
    }
  }

  if (ferror(fsrc)) {
    printf("cp: error reading from '%s': %s\n", src, strerror(errno));
    ret = -1;
  }

  free(buf);
  fclose(fsrc);
  fclose(fdst);

  if (ret == 0 && verbose) {
    printf("'%s' -> '%s'\n", src, dst);
  }

  return ret;
}

typedef struct {
  bool force;
  bool verbose;
} cp_walk_ctx_t;

static int copy_directory_recursive(const char *src, const char *dst, bool force, bool verbose);

static int cp_walk_entry(const char *src, const char *dst, bool is_dir, void *ctx) {
  cp_walk_ctx_t *c = (cp_walk_ctx_t *)ctx;
  if (is_dir) {
    return copy_directory_recursive(src, dst, c->force, c->verbose);
  }
  return copy_single_file(src, dst, c->force, c->verbose);
}

static int copy_directory_recursive(const char *src, const char *dst, bool force, bool verbose) {
  struct stat st_src;
  if (stat(src, &st_src) != 0) {
    printf("cp: cannot stat '%s': %s\n", src, strerror(errno));
    return -1;
  }

  if (mkdir(dst, 0755) != 0 && errno != EEXIST) {
    printf("cp: cannot create directory '%s': %s\n", dst, strerror(errno));
    return -1;
  }

  DIR *dir = opendir(src);
  if (!dir) {
    printf("cp: cannot open directory '%s': %s\n", src, strerror(errno));
    return -1;
  }

  cp_walk_ctx_t ctx = {.force = force, .verbose = verbose};
  return fs_walk_dir_pairs(dir, src, dst, cp_walk_entry, &ctx);
}

int cp_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return -1;
  }

  bool force = false;
  bool recursive = false;
  bool verbose = false;
  int first_file = 0;
  fs_opts_config_t opt_cfg = {.tool_name = "cp", .print_help = print_usage, .allow_recursive = true};
  if (fs_parse_options(argc, argv, &opt_cfg, &force, &recursive, &verbose, &first_file) != 0) {
    return -1;
  }
  if (first_file == -2) {
    return 0;
  }

  int num_sources = (argc - 1) - first_file;
  if (first_file == 0 || num_sources < 1 || argc < 3) {
    printf("cp: missing destination file operand\n");
    return -1;
  }

  const char *dest = argv[argc - 1];
  bool dest_is_dir = is_directory(dest);

  if (num_sources > 1 && !dest_is_dir) {
    printf("cp: target '%s' is not a directory\n", dest);
    return -1;
  }

  int ret = 0;
  for (int i = first_file; i < argc - 1; i++) {
    const char *src = argv[i];
    struct stat st_src;

    if (stat(src, &st_src) != 0) {
      printf("cp: cannot stat '%s': %s\n", src, strerror(errno));
      ret = -1;
      continue;
    }

    char target_path[PATH_MAX];
    if (dest_is_dir) {
      const char *base = get_basename(src);
      join_path(target_path, sizeof(target_path), dest, base);
    } else {
      strlcpy(target_path, dest, sizeof(target_path));
    }

    if (S_ISDIR(st_src.st_mode)) {
      if (!recursive) {
        printf("cp: -r not specified; omitting directory '%s'\n", src);
        ret = -1;
        continue;
      }
      if (copy_directory_recursive(src, target_path, force, verbose) != 0) {
        ret = -1;
      }
    } else {
      if (copy_single_file(src, target_path, force, verbose) != 0) {
        ret = -1;
      }
    }
  }

  return ret;
}

EOS_BIN_ATTR eos_bin_t cp_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "cp",
    .name = "Copy File",
    .group = "Utils",
    .description = "Copy files or directories",
    .entry_point = cp_main,
};
