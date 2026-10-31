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

static const char *get_basename(const char *path) {
  if (!path || !path[0]) return "";
  const char *p = path + strlen(path) - 1;
  while (p > path && *p == '/') p--;
  while (p > path && *(p - 1) != '/') p--;
  return p;
}

static void join_path(char *dst, size_t dst_size, const char *dir, const char *name) {
  size_t len = strlen(dir);
  if (len > 0 && dir[len - 1] == '/') {
    snprintf(dst, dst_size, "%s%s", dir, name);
  } else {
    snprintf(dst, dst_size, "%s/%s", dir, name);
  }
}

static bool is_directory(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return false;
}

static bool is_same_file(const char *src, const char *dst) {
  if (strcmp(src, dst) == 0) {
    return true;
  }
  struct stat st_src, st_dst;
  if (stat(src, &st_src) == 0 && stat(dst, &st_dst) == 0) {
    if (st_src.st_dev == st_dst.st_dev && st_src.st_ino == st_dst.st_ino && st_src.st_ino != 0) {
      return true;
    }
  }
  return false;
}

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

  char buf[CP_BUFFER_SIZE];
  size_t n_read = 0;
  int ret = 0;

  while ((n_read = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
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

  fclose(fsrc);
  fclose(fdst);

  if (ret == 0 && verbose) {
    printf("'%s' -> '%s'\n", src, dst);
  }

  return ret;
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

  int ret = 0;
  struct dirent *entry;
  char child_src[PATH_MAX];
  char child_dst[PATH_MAX];

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    join_path(child_src, sizeof(child_src), src, entry->d_name);
    join_path(child_dst, sizeof(child_dst), dst, entry->d_name);

    struct stat st_child;
    if (stat(child_src, &st_child) != 0) {
      printf("cp: cannot stat '%s': %s\n", child_src, strerror(errno));
      ret = -1;
      continue;
    }

    if (S_ISDIR(st_child.st_mode)) {
      if (copy_directory_recursive(child_src, child_dst, force, verbose) != 0) {
        ret = -1;
      }
    } else {
      if (copy_single_file(child_src, child_dst, force, verbose) != 0) {
        ret = -1;
      }
    }
  }

  closedir(dir);
  return ret;
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

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && argv[i][1] != '\0') {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        print_usage();
        return 0;
      }
      if (strcmp(argv[i], "--force") == 0) {
        force = true;
        continue;
      }
      if (strcmp(argv[i], "--recursive") == 0) {
        recursive = true;
        continue;
      }
      if (strcmp(argv[i], "--verbose") == 0) {
        verbose = true;
        continue;
      }
      if (strcmp(argv[i], "--") == 0) {
        first_file = i + 1;
        break;
      }

      bool valid = true;
      for (size_t c = 1; argv[i][c] != '\0'; c++) {
        switch (argv[i][c]) {
        case 'f': force = true; break;
        case 'r':
        case 'R': recursive = true; break;
        case 'v': verbose = true; break;
        default:
          printf("cp: invalid option -- '%c'\n", argv[i][c]);
          valid = false;
          break;
        }
      }
      if (!valid) {
        return -1;
      }
    } else {
      first_file = i;
      break;
    }
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
