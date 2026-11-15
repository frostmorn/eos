#pragma once

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static inline const char *fs_get_basename(const char *path) {
  if (!path || !path[0]) return "";
  const char *p = path + strlen(path) - 1;
  while (p > path && *p == '/') p--;
  while (p > path && *(p - 1) != '/') p--;
  return p;
}

static inline void fs_join_path(char *dst, size_t dst_size, const char *dir, const char *name) {
  size_t len = strlen(dir);
  if (len > 0 && dir[len - 1] == '/') {
    snprintf(dst, dst_size, "%s%s", dir, name);
  } else {
    snprintf(dst, dst_size, "%s/%s", dir, name);
  }
}

static inline bool fs_is_directory(const char *path) {
  struct stat st;
  return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static inline bool fs_is_same_file(const char *src, const char *dst) {
  if (strcmp(src, dst) == 0) return true;
  struct stat st_src, st_dst;
  if (stat(src, &st_src) == 0 && stat(dst, &st_dst) == 0) {
    return (st_src.st_ino != 0 && st_src.st_ino == st_dst.st_ino && st_src.st_dev == st_dst.st_dev);
  }
  return false;
}

typedef struct {
  char *src;
  char *dst;
} fs_pair_paths_t;

static inline bool fs_alloc_pair(fs_pair_paths_t *pair, size_t size) {
  pair->src = malloc(size);
  pair->dst = malloc(size);
  if (!pair->src || !pair->dst) {
    if (pair->src) free(pair->src);
    if (pair->dst) free(pair->dst);
    pair->src = pair->dst = NULL;
    return false;
  }
  return true;
}

static inline void fs_free_pair(fs_pair_paths_t *pair) {
  if (pair->src) free(pair->src);
  if (pair->dst) free(pair->dst);
  pair->src = pair->dst = NULL;
}

static inline int fs_walk_dir_pairs(DIR *dir, const char *src, const char *dst,
                                    int (*entry_cb)(const char *src_child, const char *dst_child, bool is_dir, void *ctx),
                                    void *ctx) {
  int ret = 0;
  struct dirent *entry;
  fs_pair_paths_t p;
  if (!fs_alloc_pair(&p, 4096)) {
    closedir(dir);
    return -1;
  }
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    fs_join_path(p.src, 4096, src, entry->d_name);
    fs_join_path(p.dst, 4096, dst, entry->d_name);

    struct stat st_child;
    if (stat(p.src, &st_child) != 0) {
      ret = -1;
      continue;
    }
    if (entry_cb(p.src, p.dst, S_ISDIR(st_child.st_mode), ctx) != 0) {
      ret = -1;
    }
  }
  fs_free_pair(&p);
  closedir(dir);
  return ret;
}

typedef struct {
  const char *tool_name;
  void (*print_help)(void);
  bool allow_recursive;
} fs_opts_config_t;

static inline int fs_parse_options(int argc, char **argv, const fs_opts_config_t *cfg,
                                   bool *out_force, bool *out_recursive, bool *out_verbose, int *out_first_file) {
  *out_force = false;
  if (out_recursive) *out_recursive = false;
  *out_verbose = false;
  *out_first_file = 0;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && argv[i][1] != '\0') {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        if (cfg->print_help) cfg->print_help();
        *out_first_file = -2;
        return 0;
      }
      if (strcmp(argv[i], "--force") == 0) {
        *out_force = true;
        continue;
      }
      if (cfg->allow_recursive && strcmp(argv[i], "--recursive") == 0) {
        if (out_recursive) *out_recursive = true;
        continue;
      }
      if (strcmp(argv[i], "--verbose") == 0) {
        *out_verbose = true;
        continue;
      }
      if (strcmp(argv[i], "--") == 0) {
        *out_first_file = i + 1;
        break;
      }

      bool valid = true;
      for (size_t c = 1; argv[i][c] != '\0'; c++) {
        switch (argv[i][c]) {
        case 'f': *out_force = true; break;
        case 'r':
        case 'R':
          if (cfg->allow_recursive && out_recursive) {
            *out_recursive = true;
            break;
          }
          printf("%s: invalid option -- '%c'\n", cfg->tool_name ? cfg->tool_name : "fs", argv[i][c]);
          valid = false;
          break;
        case 'v': *out_verbose = true; break;
        default:
          printf("%s: invalid option -- '%c'\n", cfg->tool_name ? cfg->tool_name : "fs", argv[i][c]);
          valid = false;
          break;
        }
      }
      if (!valid) return -1;
    } else {
      *out_first_file = i;
      break;
    }
  }
  return 0;
}
