#include "term_logic.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *s_preset_cmds[TERM_PRESET_COUNT] = {
    "ls -l /bin",
    "cat /eos.log",
    "env",
    "tree /",
    "ls /bin",
    "ls /dev",
    "cat /proc/heapinfo",
    "cat /proc/mounts",
    "cat /proc/fds",
    "cat /proc/gpio",
    "cat /proc/interrupts",
    "cat /proc/uptime",
    "pwd",
};

static void term_ensure_empty_input_file(void) {
  int fd = open(TERM_DEFAULT_IN_FILE, O_WRONLY | O_CREAT | O_TRUNC, EOS_DEFAULT_FILE_MODE);
  if (fd >= 0) {
    close(fd);
  }
}

int term_logic_init(term_logic_t *tl) {
  if (!tl) return -1;
  memset(tl, 0, sizeof(*tl));
  tl->text_buf = calloc(1, TERM_MAX_TEXT_BYTES);
  if (!tl->text_buf) return -1;

  tl->preset_idx = 0;
  snprintf(tl->active_cmd, sizeof(tl->active_cmd), "%s", s_preset_cmds[0]);
  snprintf(tl->title, sizeof(tl->title), "Terminal");
  return 0;
}

void term_logic_deinit(term_logic_t *tl) {
  if (!tl) return;
  if (tl->text_buf) {
    free(tl->text_buf);
    tl->text_buf = NULL;
  }
}

const char *term_logic_get_preset(int idx) {
  if (idx < 0 || idx >= TERM_PRESET_COUNT) return "";
  return s_preset_cmds[idx];
}

void term_logic_set_preset(term_logic_t *tl, int idx) {
  if (!tl) return;
  if (idx < 0) idx = 0;
  if (idx >= TERM_PRESET_COUNT) idx = TERM_PRESET_COUNT - 1;
  tl->preset_idx = idx;
  snprintf(tl->active_cmd, sizeof(tl->active_cmd), "%s", s_preset_cmds[idx]);
}

void term_logic_next_preset(term_logic_t *tl) {
  if (!tl) return;
  tl->preset_idx = (tl->preset_idx + 1) % TERM_PRESET_COUNT;
  snprintf(tl->active_cmd, sizeof(tl->active_cmd), "%s", s_preset_cmds[tl->preset_idx]);
}

void term_logic_prev_preset(term_logic_t *tl) {
  if (!tl) return;
  tl->preset_idx = (tl->preset_idx - 1 + TERM_PRESET_COUNT) % TERM_PRESET_COUNT;
  snprintf(tl->active_cmd, sizeof(tl->active_cmd), "%s", s_preset_cmds[tl->preset_idx]);
}

void term_logic_set_custom_cmd(term_logic_t *tl, const char *cmd) {
  if (!tl || !cmd) return;
  snprintf(tl->active_cmd, sizeof(tl->active_cmd), "%s", cmd);
}

int term_logic_sanitize_text(const char *raw, size_t raw_len, char *dst,
                            size_t dst_cap, size_t *out_bytes) {
  if (!dst || dst_cap == 0) return 0;
  size_t r = 0;
  size_t w = 0;
  int lines = 1;

  while (r < raw_len && w + 1 < dst_cap) {
    unsigned char ch = (unsigned char)raw[r];

    if (ch == 0x1B) {
      r++;
      if (r < raw_len && raw[r] == '[') {
        r++;
        while (r < raw_len) {
          unsigned char c = (unsigned char)raw[r++];
          if ((c >= '@' && c <= '~') || c == 'm') {
            break;
          }
        }
      }
      continue;
    }

    if (ch == 0xE2 && r + 2 < raw_len) {
      unsigned char b1 = (unsigned char)raw[r + 1];
      unsigned char b2 = (unsigned char)raw[r + 2];
      if (b1 == 0x94) {
        if (b2 == 0x80) { dst[w++] = '-'; r += 3; continue; }
        if (b2 == 0x82) { dst[w++] = '|'; r += 3; continue; }
        if (b2 == 0x94) { dst[w++] = '`'; r += 3; continue; }
        if (b2 == 0x9C) { dst[w++] = '+'; r += 3; continue; }
      }
      dst[w++] = '?';
      r += 3;
      continue;
    }

    if (ch >= 0x80) {
      r++;
      dst[w++] = '?';
      continue;
    }

    if (ch == '\r') {
      r++;
      continue;
    }

    if (ch == '\t') {
      int spaces = 2;
      while (spaces-- > 0 && w + 1 < dst_cap) {
        dst[w++] = ' ';
      }
      r++;
      continue;
    }

    if (ch == '\n') {
      dst[w++] = '\n';
      lines++;
      r++;
      continue;
    }

    if (ch >= 0x20 && ch <= 0x7E) {
      dst[w++] = (char)ch;
    }
    r++;
  }

  dst[w] = '\0';
  if (out_bytes) {
    *out_bytes = w;
  }
  return lines;
}

int term_logic_load_fd(term_logic_t *tl, int fd, const char *save_copy_path) {
  if (!tl || !tl->text_buf) return -1;
  char *raw = malloc(TERM_MAX_TEXT_BYTES);
  if (!raw) return -1;

  size_t total = 0;
  while (total + 1 < TERM_MAX_TEXT_BYTES) {
    ssize_t n = read(fd, raw + total, TERM_MAX_TEXT_BYTES - 1 - total);
    if (n > 0) {
      total += (size_t)n;
    } else if (n == 0) {
      break;
    } else {
      if (errno == EAGAIN || errno == EINTR) {
        usleep(5000);
        continue;
      }
      break;
    }
  }
  raw[total] = '\0';

  if (save_copy_path && save_copy_path[0]) {
    int out_fd = open(save_copy_path, O_WRONLY | O_CREAT | O_TRUNC, EOS_DEFAULT_FILE_MODE);
    if (out_fd >= 0) {
      if (total > 0) {
        (void)write(out_fd, raw, total);
      }
      close(out_fd);
    }
  }

  tl->total_lines = term_logic_sanitize_text(raw, total, tl->text_buf,
                                            TERM_MAX_TEXT_BYTES, &tl->total_bytes);
  free(raw);

  if (tl->total_bytes == 0) {
    snprintf(tl->text_buf, TERM_MAX_TEXT_BYTES,
             "(Command completed with no stdout/stderr output)");
    tl->total_bytes = strlen(tl->text_buf);
    tl->total_lines = 1;
  }
  return 0;
}

int term_logic_load_file(term_logic_t *tl, const char *path) {
  if (!tl || !path) return -1;
  int fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    snprintf(tl->text_buf, TERM_MAX_TEXT_BYTES,
             "No output file at '%s'.\nPress ▶ Run or A/ENTER to run '%s'.", path,
             tl->active_cmd[0] ? tl->active_cmd : s_preset_cmds[tl->preset_idx]);
    tl->total_bytes = strlen(tl->text_buf);
    tl->total_lines = 2;
    return -1;
  }

  int rc = term_logic_load_fd(tl, fd, NULL);
  close(fd);
  return rc;
}

int term_logic_execute_cmd(term_logic_t *tl, const char *cmd) {
  if (!tl || !cmd || !cmd[0]) return -1;

  term_ensure_empty_input_file();

  const char *out_path = tl->file_path[0] ? tl->file_path : TERM_DEFAULT_OUT_FILE;
  char full_cmd[EOS_LARGE_STR_LEN];
  snprintf(full_cmd, sizeof(full_cmd), "%s < %s > %s", cmd,
           TERM_DEFAULT_IN_FILE, out_path);

  int rc = system(full_cmd);
  tl->last_exit_code = rc;
  tl->has_exit_code = true;
  snprintf(tl->active_cmd, sizeof(tl->active_cmd), "%s", cmd);
  snprintf(tl->title, sizeof(tl->title), "%.18s (exit %d)", cmd, rc);

  term_logic_load_file(tl, out_path);
  return rc;
}

void term_logic_format_meta(const term_logic_t *tl, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (!tl) {
    buf[0] = '\0';
    return;
  }
  if (tl->has_exit_code) {
    snprintf(buf, buflen, "%d lines, %u B [exit %d]", tl->total_lines,
             (unsigned)tl->total_bytes, tl->last_exit_code);
  } else {
    snprintf(buf, buflen, "%d lines, %u B", tl->total_lines, (unsigned)tl->total_bytes);
  }
}
