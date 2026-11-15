#pragma once

#include "ecore/ioctl.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static inline ssize_t bar_read_file(const char *path, char *buf, size_t max_len) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    buf[0] = '\0';
    return -1;
  }
  ssize_t n = read(fd, buf, max_len - 1);
  if (n > 0) {
    buf[n] = '\0';
  } else {
    buf[0] = '\0';
  }
  close(fd);
  return n;
}

static inline bool bar_validate_pid_prefix(char *line, const char **out_text) {
  *out_text = line;
  if (line[0] == '@') {
    char *endp = NULL;
    long pid = strtol(line + 1, &endp, 10);
    if (endp && *endp == ' ') {
      char proc_path[32];
      snprintf(proc_path, sizeof(proc_path), "/proc/%ld", pid);
      struct stat st;
      if (stat(proc_path, &st) != 0) {
        return false;
      }
      *out_text = endp + 1;
    }
  }
  return true;
}

static inline void bar_parse_args(int argc, char **argv, bool *out_auto, int *out_timeout) {
  *out_auto = false;
  *out_timeout = -1;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--auto") == 0) {
      *out_auto = true;
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      *out_timeout = atoi(argv[++i]);
    }
  }
}

static inline void bar_split_lines(char *buf, char **out_line1, char **out_line2) {
  *out_line1 = buf;
  *out_line2 = NULL;
  char *nl = strchr(buf, '\n');
  if (nl) {
    *nl = '\0';
    *out_line2 = nl + 1;
    char *nl2 = strchr(*out_line2, '\n');
    if (nl2) *nl2 = '\0';
    nl2 = strchr(*out_line2, '\r');
    if (nl2) *nl2 = '\0';
  }
  char *cr = strchr(*out_line1, '\r');
  if (cr) *cr = '\0';
}

static inline void bar_query_display_size(int fd, uint32_t *w, uint32_t *h,
                                         uint32_t default_w, uint32_t default_h) {
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, w) != 0 || *w == 0) {
    *w = default_w;
  }
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, h) != 0 || *h == 0) {
    *h = default_h;
  }
}
