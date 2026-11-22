#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

#define TERM_DEFAULT_OUT_FILE "/term.out"
#define TERM_DEFAULT_IN_FILE  "/term.in"
#define TERM_MAX_TEXT_BYTES   16384
#define TERM_PRESET_COUNT     13

typedef struct {
  char file_path[EOS_MID_STR_LEN];
  char title[EOS_SMALL_STR_LEN];
  char active_cmd[EOS_MID_STR_LEN];
  int preset_idx;
  int last_exit_code;
  bool has_exit_code;
  int total_lines;
  size_t total_bytes;
  char *text_buf;
} term_logic_t;

int term_logic_init(term_logic_t *tl);
void term_logic_deinit(term_logic_t *tl);

const char *term_logic_get_preset(int idx);
void term_logic_set_preset(term_logic_t *tl, int idx);
void term_logic_next_preset(term_logic_t *tl);
void term_logic_prev_preset(term_logic_t *tl);
void term_logic_set_custom_cmd(term_logic_t *tl, const char *cmd);

int term_logic_sanitize_text(const char *raw, size_t raw_len, char *dst, size_t dst_cap, size_t *out_bytes);
int term_logic_load_file(term_logic_t *tl, const char *path);
int term_logic_load_fd(term_logic_t *tl, int fd, const char *save_copy_path);
int term_logic_execute_cmd(term_logic_t *tl, const char *cmd);
void term_logic_format_meta(const term_logic_t *tl, char *buf, size_t buflen);
