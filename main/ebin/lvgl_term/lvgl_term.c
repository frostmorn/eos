#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ecore/proc.h"
#include "emisc/fancymacro.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define TERM_DEFAULT_OUT_FILE "/term.out"
#define TERM_DEFAULT_IN_FILE  "/term.in"
#define TERM_MAX_TEXT_BYTES   16384

static const char *const s_preset_cmds[] = {
    "cat /proc/cpuinfo",
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

#define TERM_PRESET_COUNT (int)(sizeof(s_preset_cmds) / sizeof(s_preset_cmds[0]))

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
  bool should_exit;
  bool trigger_run;
  bool is_editing;

  // Timeout for automated testing (-t <ms>)
  uint32_t timeout_ms;
  uint64_t start_time_ms;

  // UI elements
  lv_obj_t *hdr_title;
  lv_obj_t *hdr_meta;
  lv_obj_t *cmd_box;
  lv_obj_t *cmd_label;
  lv_obj_t *btn_prev;
  lv_obj_t *btn_next;
  lv_obj_t *btn_kb;
  lv_obj_t *btn_run;
  lv_obj_t *viewport;
  lv_obj_t *out_label;
  lv_obj_t *footer_label;

  lv_obj_t *edit_overlay;
  lv_obj_t *edit_ta;
  lv_obj_t *edit_kb;
} term_viewer_state_t;

static uint64_t get_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static void ensure_empty_input_file(void) {
  int fd = open(TERM_DEFAULT_IN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    close(fd);
  }
}

// Forward declarations
static void term_update_ui(term_viewer_state_t *st);
static void term_open_edit_mode(term_viewer_state_t *st);
static void term_close_edit_mode(term_viewer_state_t *st);
static int term_load_from_file(term_viewer_state_t *st, const char *path);
static int term_execute_cmd_to_file(term_viewer_state_t *st, const char *cmd);

// Sanitize raw terminal text for LVGL's built-in font:
// - Strip ANSI CSI escape sequences (\x1b[...m)
// - Convert UTF-8 box-drawing chars (used by `tree`) to ASCII equivalents
// - Normalize \r\n and \t
static int sanitize_terminal_text(const char *raw, size_t raw_len, char *dst,
                                  size_t dst_cap, size_t *out_bytes) {
  if (!dst || dst_cap == 0)
    return 0;

  size_t r = 0;
  size_t w = 0;
  int lines = 1;

  while (r < raw_len && w + 1 < dst_cap) {
    unsigned char ch = (unsigned char)raw[r];

    // Strip ANSI escape sequences: ESC '[' ... [A-Za-z~]
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

    // Convert UTF-8 box-drawing sequences (0xE2 0x94 0x80..0xBF) used by `tree`
    if (ch == 0xE2 && r + 2 < raw_len) {
      if ((unsigned char)raw[r + 1] == 0x94) {
        unsigned char b3 = (unsigned char)raw[r + 2];
        char repl = '-';
        if (b3 == 0x82)      repl = '|'; // │
        else if (b3 == 0x9C) repl = '+'; // ├
        else if (b3 == 0x94) repl = '`'; // └
        else if (b3 == 0x80) repl = '-'; // ─
        dst[w++] = repl;
        r += 3;
        continue;
      }
    }

    // Skip other multi-byte UTF-8 sequences not supported in ASCII font
    if (ch >= 0x80) {
      r++;
      while (r < raw_len && (((unsigned char)raw[r] & 0xC0) == 0x80)) {
        r++;
      }
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

static int term_load_from_fd(term_viewer_state_t *st, int fd,
                             const char *save_copy_path) {
  char *raw = malloc(TERM_MAX_TEXT_BYTES);
  if (!raw)
    return -1;

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
    int out_fd = open(save_copy_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd >= 0) {
      if (total > 0) {
        (void)write(out_fd, raw, total);
      }
      close(out_fd);
    }
  }

  st->total_lines = sanitize_terminal_text(raw, total, st->text_buf,
                                           TERM_MAX_TEXT_BYTES, &st->total_bytes);
  free(raw);

  if (st->total_bytes == 0) {
    snprintf(st->text_buf, TERM_MAX_TEXT_BYTES,
             "(Command completed with no stdout/stderr output)");
    st->total_bytes = strlen(st->text_buf);
    st->total_lines = 1;
  }
  return 0;
}

static int term_load_from_file(term_viewer_state_t *st, const char *path) {
  int fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    snprintf(st->text_buf, TERM_MAX_TEXT_BYTES,
             "No output file at '%s'.\nPress ▶ Run or A/ENTER to run '%s'.", path,
             st->active_cmd[0] ? st->active_cmd : s_preset_cmds[st->preset_idx]);
    st->total_bytes = strlen(st->text_buf);
    st->total_lines = 2;
    return -1;
  }

  int rc = term_load_from_fd(st, fd, NULL);
  close(fd);
  return rc;
}

static int term_execute_cmd_to_file(term_viewer_state_t *st, const char *cmd) {
  if (!cmd || !cmd[0])
    return -1;

  ensure_empty_input_file();

  const char *out_path =
      st->file_path[0] ? st->file_path : TERM_DEFAULT_OUT_FILE;
  char full_cmd[EOS_LARGE_STR_LEN];
  snprintf(full_cmd, sizeof(full_cmd), "%s < %s > %s", cmd,
           TERM_DEFAULT_IN_FILE, out_path);

  int rc = system(full_cmd);
  st->last_exit_code = rc;
  st->has_exit_code = true;
  snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", cmd);
  snprintf(st->title, sizeof(st->title), "%.18s (exit %d)", cmd, rc);

  term_load_from_file(st, out_path);
  return rc;
}

// Proportional scrolling based on viewport dimensions
static void term_scroll_by(term_viewer_state_t *st, int32_t dy) {
  if (!st->viewport)
    return;

  if (dy > 0) {
    int32_t bottom = lv_obj_get_scroll_bottom(st->viewport);
    if (bottom <= 0)
      return;
    if (dy > bottom)
      dy = bottom;
    lv_obj_scroll_by(st->viewport, 0, -dy, LV_ANIM_OFF);
  } else if (dy < 0) {
    int32_t top = lv_obj_get_scroll_top(st->viewport);
    if (top <= 0)
      return;
    int32_t up = -dy;
    if (up > top)
      up = top;
    lv_obj_scroll_by(st->viewport, 0, up, LV_ANIM_OFF);
  }

  term_update_ui(st);
}

static void term_scroll_line_up(term_viewer_state_t *st) {
  if (!st->viewport) return;
  int32_t vp_h = lv_obj_get_height(st->viewport);
  if (vp_h <= 0) vp_h = (int32_t)eos_lvgl_get_height() - 66;
  int32_t dy = vp_h / 8; // 1/8th of viewport height
  if (dy < 16) dy = 16;
  term_scroll_by(st, -dy);
}

static void term_scroll_line_down(term_viewer_state_t *st) {
  if (!st->viewport) return;
  int32_t vp_h = lv_obj_get_height(st->viewport);
  if (vp_h <= 0) vp_h = (int32_t)eos_lvgl_get_height() - 66;
  int32_t dy = vp_h / 8; // 1/8th of viewport height
  if (dy < 16) dy = 16;
  term_scroll_by(st, dy);
}

static void term_scroll_page_up(term_viewer_state_t *st) {
  if (!st->viewport) return;
  int32_t vp_h = lv_obj_get_height(st->viewport);
  if (vp_h <= 0) vp_h = (int32_t)eos_lvgl_get_height() - 66;
  int32_t dy = (vp_h * 3) / 4; // 3/4th (75%) of viewport height
  if (dy < 32) dy = 32;
  term_scroll_by(st, -dy);
}

static void term_scroll_page_down(term_viewer_state_t *st) {
  if (!st->viewport) return;
  int32_t vp_h = lv_obj_get_height(st->viewport);
  if (vp_h <= 0) vp_h = (int32_t)eos_lvgl_get_height() - 66;
  int32_t dy = (vp_h * 3) / 4; // 3/4th (75%) of viewport height
  if (dy < 32) dy = 32;
  term_scroll_by(st, dy);
}

static void term_update_ui(term_viewer_state_t *st) {
  if (!st->hdr_title)
    return;

  lv_label_set_text(st->hdr_title,
                    st->title[0] ? st->title : "Terminal Viewer");

  if (st->hdr_meta) {
    char mbuf[48];
    snprintf(mbuf, sizeof(mbuf), "%dL %s", st->total_lines,
             eos_lvgl_is_full_fallback() ? "FULL" : "PART");
    lv_label_set_text(st->hdr_meta, mbuf);
  }

  if (st->cmd_label) {
    const char *cmd =
        st->active_cmd[0] ? st->active_cmd : s_preset_cmds[st->preset_idx];
    char cbuf[96];
    snprintf(cbuf, sizeof(cbuf), "%.48s", cmd);
    lv_label_set_text(st->cmd_label, cbuf);
  }

  if (st->out_label && st->text_buf) {
    lv_label_set_text(st->out_label, st->text_buf);
  }

  if (st->footer_label) {
    int32_t sy = st->viewport ? lv_obj_get_scroll_y(st->viewport) : 0;
    int32_t bottom = st->viewport ? lv_obj_get_scroll_bottom(st->viewport) : 0;
    char fbuf[96];
    snprintf(fbuf, sizeof(fbuf),
             "U/D:Scrl(%d/%d)  L/R:Preset  A:Run  D/KB:Edit  B:Exit", (int)sy,
             (int)(sy + (bottom > 0 ? bottom : 0)));
    lv_label_set_text(st->footer_label, fbuf);
  }
}

// Touch button event handlers
static void term_btn_prev_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  st->preset_idx = (st->preset_idx - 1 + TERM_PRESET_COUNT) % TERM_PRESET_COUNT;
  snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", s_preset_cmds[st->preset_idx]);
  term_update_ui(st);
}

static void term_btn_next_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  st->preset_idx = (st->preset_idx + 1) % TERM_PRESET_COUNT;
  snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", s_preset_cmds[st->preset_idx]);
  term_update_ui(st);
}

static void term_btn_kb_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  term_open_edit_mode(st);
}

static void term_btn_run_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  st->trigger_run = true;
}

static void term_cmd_box_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  term_open_edit_mode(st);
}

static void term_build_ui(term_viewer_state_t *st) {
  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  bool is_large = (h >= 400);
  int32_t hdr_h = is_large ? 32 : 22;
  int32_t cbar_h = is_large ? 38 : 24;
  int32_t ftr_h = is_large ? 26 : 20;

  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1120), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // 1. Header bar
  lv_obj_t *hdr = lv_obj_create(scr);
  lv_obj_set_size(hdr, (int32_t)w, hdr_h);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1E3A8A), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_hor(hdr, 6, 0);
  lv_obj_set_style_pad_ver(hdr, is_large ? 4 : 2, 0);

  st->hdr_title = lv_label_create(hdr);
  lv_obj_set_width(st->hdr_title, (int32_t)w - (is_large ? 150 : 86));
  lv_label_set_long_mode(st->hdr_title, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->hdr_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(st->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);

  st->hdr_meta = lv_label_create(hdr);
  lv_obj_set_style_text_color(st->hdr_meta, lv_color_hex(0x34D399), 0);
  lv_obj_align(st->hdr_meta, LV_ALIGN_RIGHT_MID, 0, 0);

  // 2. Command bar with dedicated touch buttons (Prev, Cmd Box, Next, KB, Run)
  lv_obj_t *cbar = lv_obj_create(scr);
  lv_obj_set_size(cbar, (int32_t)w, cbar_h);
  lv_obj_align(cbar, LV_ALIGN_TOP_MID, 0, hdr_h);
  lv_obj_set_style_bg_color(cbar, lv_color_hex(0x172554), 0);
  lv_obj_set_style_border_width(cbar, 0, 0);
  lv_obj_set_style_radius(cbar, 0, 0);
  lv_obj_set_style_pad_hor(cbar, 4, 0);
  lv_obj_set_style_pad_ver(cbar, 2, 0);

  int32_t nav_w = is_large ? 34 : 22;
  int32_t kb_w = is_large ? 86 : 32;
  int32_t run_w = is_large ? 74 : 28;
  int32_t btn_h = cbar_h - 4;

  // Previous preset button [<]
  st->btn_prev = lv_button_create(cbar);
  lv_obj_set_size(st->btn_prev, nav_w, btn_h);
  lv_obj_align(st->btn_prev, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(st->btn_prev, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(st->btn_prev, 3, 0);
  lv_obj_set_style_pad_all(st->btn_prev, 0, 0);
  lv_obj_add_event_cb(st->btn_prev, term_btn_prev_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_prev = lv_label_create(st->btn_prev);
  lv_label_set_text(lbl_prev, "<");
  lv_obj_align(lbl_prev, LV_ALIGN_CENTER, 0, 0);

  // Next preset button [>]
  st->btn_next = lv_button_create(cbar);
  lv_obj_set_size(st->btn_next, nav_w, btn_h);
  lv_obj_align(st->btn_next, LV_ALIGN_LEFT_MID, nav_w + 3, 0);
  lv_obj_set_style_bg_color(st->btn_next, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_radius(st->btn_next, 3, 0);
  lv_obj_set_style_pad_all(st->btn_next, 0, 0);
  lv_obj_add_event_cb(st->btn_next, term_btn_next_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_next = lv_label_create(st->btn_next);
  lv_label_set_text(lbl_next, ">");
  lv_obj_align(lbl_next, LV_ALIGN_CENTER, 0, 0);

  // Run button [▶ Run]
  st->btn_run = lv_button_create(cbar);
  lv_obj_set_size(st->btn_run, run_w, btn_h);
  lv_obj_align(st->btn_run, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(st->btn_run, lv_color_hex(0x15803D), 0);
  lv_obj_set_style_radius(st->btn_run, 3, 0);
  lv_obj_set_style_pad_all(st->btn_run, 0, 0);
  lv_obj_add_event_cb(st->btn_run, term_btn_run_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_run = lv_label_create(st->btn_run);
  lv_label_set_text(lbl_run, is_large ? "▶ Run" : "▶");
  lv_obj_set_style_text_color(lbl_run, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_run, LV_ALIGN_CENTER, 0, 0);

  // Keyboard Edit button [⌨ Edit]
  st->btn_kb = lv_button_create(cbar);
  lv_obj_set_size(st->btn_kb, kb_w, btn_h);
  lv_obj_align(st->btn_kb, LV_ALIGN_RIGHT_MID, -(run_w + 3), 0);
  lv_obj_set_style_bg_color(st->btn_kb, lv_color_hex(0x2563EB), 0);
  lv_obj_set_style_radius(st->btn_kb, 3, 0);
  lv_obj_set_style_pad_all(st->btn_kb, 0, 0);
  lv_obj_add_event_cb(st->btn_kb, term_btn_kb_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_kb = lv_label_create(st->btn_kb);
  lv_label_set_text(lbl_kb, is_large ? "⌨ Edit" : "⌨");
  lv_obj_set_style_text_color(lbl_kb, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_kb, LV_ALIGN_CENTER, 0, 0);

  // Command box (clickable to open editor)
  int32_t cmd_box_x = (nav_w * 2) + 6;
  int32_t cmd_box_w = (int32_t)w - cmd_box_x - (kb_w + run_w + 10);
  if (cmd_box_w < 60) cmd_box_w = 60;

  st->cmd_box = lv_button_create(cbar);
  lv_obj_set_size(st->cmd_box, cmd_box_w, btn_h);
  lv_obj_align(st->cmd_box, LV_ALIGN_LEFT_MID, cmd_box_x, 0);
  lv_obj_set_style_bg_color(st->cmd_box, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_color(st->cmd_box, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(st->cmd_box, 1, 0);
  lv_obj_set_style_radius(st->cmd_box, 3, 0);
  lv_obj_set_style_pad_hor(st->cmd_box, 5, 0);
  lv_obj_set_style_pad_ver(st->cmd_box, 0, 0);
  lv_obj_add_event_cb(st->cmd_box, term_cmd_box_cb, LV_EVENT_CLICKED, st);

  st->cmd_label = lv_label_create(st->cmd_box);
  lv_obj_set_width(st->cmd_label, cmd_box_w - 10);
  lv_label_set_long_mode(st->cmd_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->cmd_label, lv_color_hex(0xFDE047), 0);
  lv_obj_align(st->cmd_label, LV_ALIGN_LEFT_MID, 0, 0);

  // 3. Scrollable terminal output viewport
  int32_t vp_y = hdr_h + cbar_h + 2;
  int32_t vp_h = (int32_t)h - vp_y - ftr_h - 2;
  if (vp_h < 60) vp_h = 60;

  st->viewport = lv_obj_create(scr);
  lv_obj_set_size(st->viewport, (int32_t)w - 4, vp_h);
  lv_obj_align(st->viewport, LV_ALIGN_TOP_MID, 0, vp_y);
  lv_obj_set_style_bg_color(st->viewport, lv_color_hex(0x050811), 0);
  lv_obj_set_style_border_color(st->viewport, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(st->viewport, 1, 0);
  lv_obj_set_style_radius(st->viewport, 4, 0);
  lv_obj_set_style_pad_all(st->viewport, 5, 0);
  lv_obj_set_scroll_dir(st->viewport, LV_DIR_ALL);

  st->out_label = lv_label_create(st->viewport);
  lv_obj_set_width(st->out_label, (int32_t)w - 18);
  lv_label_set_long_mode(st->out_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(st->out_label, lv_color_hex(0x4ADE80), 0);
  lv_obj_align(st->out_label, LV_ALIGN_TOP_LEFT, 0, 0);

  // 4. Footer bar
  lv_obj_t *ftr = lv_obj_create(scr);
  lv_obj_set_size(ftr, (int32_t)w, ftr_h);
  lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(ftr, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_width(ftr, 0, 0);
  lv_obj_set_style_radius(ftr, 0, 0);
  lv_obj_set_style_pad_hor(ftr, 6, 0);
  lv_obj_set_style_pad_ver(ftr, 1, 0);

  st->footer_label = lv_label_create(ftr);
  lv_obj_set_width(st->footer_label, (int32_t)w - 12);
  lv_label_set_long_mode(st->footer_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->footer_label, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(st->footer_label, LV_ALIGN_LEFT_MID, 0, 0);

  term_update_ui(st);
}

// Edit overlay touch button callbacks
static void term_edit_btn_run_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  if (st->edit_ta) {
    const char *text = lv_textarea_get_text(st->edit_ta);
    if (text) {
      while (*text && isspace((unsigned char)*text)) text++;
      size_t len = strlen(text);
      while (len > 0 && isspace((unsigned char)text[len - 1])) len--;
      if (len > 0) {
        if (len >= sizeof(st->active_cmd)) len = sizeof(st->active_cmd) - 1;
        memcpy(st->active_cmd, text, len);
        st->active_cmd[len] = '\0';
      }
    }
  }
  term_close_edit_mode(st);
  st->trigger_run = true;
}

static void term_edit_btn_cancel_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  term_close_edit_mode(st);
}

static void term_kb_event_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_READY) {
    if (st->edit_ta) {
      const char *text = lv_textarea_get_text(st->edit_ta);
      if (text) {
        while (*text && isspace((unsigned char)*text)) text++;
        size_t len = strlen(text);
        while (len > 0 && isspace((unsigned char)text[len - 1])) len--;
        if (len > 0) {
          if (len >= sizeof(st->active_cmd)) len = sizeof(st->active_cmd) - 1;
          memcpy(st->active_cmd, text, len);
          st->active_cmd[len] = '\0';
        }
      }
    }
    term_close_edit_mode(st);
    st->trigger_run = true;
  } else if (code == LV_EVENT_CANCEL) {
    term_close_edit_mode(st);
  }
}

static void term_close_edit_mode(term_viewer_state_t *st) {
  if (!st->is_editing)
    return;

  st->is_editing = false;
  eos_lvgl_set_dpad_group_nav(false);

  lv_group_t *g = eos_lvgl_get_group();
  if (g && st->edit_kb) {
    lv_group_remove_obj(st->edit_kb);
  }

  if (st->edit_overlay) {
    lv_obj_delete_async(st->edit_overlay);
    st->edit_overlay = NULL;
    st->edit_ta = NULL;
    st->edit_kb = NULL;
  }

  eos_lvgl_clear_keys();
  term_update_ui(st);
}

static void term_open_edit_mode(term_viewer_state_t *st) {
  if (st->is_editing)
    return;

  st->is_editing = true;
  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();
  bool is_large = (h >= 400);

  lv_obj_t *scr = lv_screen_active();

  // Full-screen overlay container
  st->edit_overlay = lv_obj_create(scr);
  lv_obj_set_size(st->edit_overlay, (int32_t)w, (int32_t)h);
  lv_obj_align(st->edit_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(st->edit_overlay, lv_color_hex(0x050811), 0);
  lv_obj_set_style_bg_opa(st->edit_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(st->edit_overlay, 0, 0);
  lv_obj_set_style_radius(st->edit_overlay, 0, 0);
  lv_obj_set_style_pad_all(st->edit_overlay, 4, 0);

  // Top Action Bar with Title and Touch Buttons [▶ Run] [✕ Cancel]
  int32_t top_h = is_large ? 34 : 26;
  lv_obj_t *top_bar = lv_obj_create(st->edit_overlay);
  lv_obj_set_size(top_bar, (int32_t)w - 8, top_h);
  lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_width(top_bar, 0, 0);
  lv_obj_set_style_pad_all(top_bar, 2, 0);

  lv_obj_t *title = lv_label_create(top_bar);
  lv_label_set_text(title, is_large ? "Command Editor" : "Edit Cmd");
  lv_obj_set_style_text_color(title, lv_color_hex(0x60A5FA), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

  // Cancel Touch Button
  int32_t btn_top_w = is_large ? 76 : 50;
  lv_obj_t *btn_cancel = lv_button_create(top_bar);
  lv_obj_set_size(btn_cancel, btn_top_w, top_h - 4);
  lv_obj_align(btn_cancel, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_radius(btn_cancel, 3, 0);
  lv_obj_set_style_pad_all(btn_cancel, 0, 0);
  lv_obj_add_event_cb(btn_cancel, term_edit_btn_cancel_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_c = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_c, is_large ? "✕ Cancel" : "Cancel");
  lv_obj_set_style_text_color(lbl_c, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_c, LV_ALIGN_CENTER, 0, 0);

  // Run Touch Button
  lv_obj_t *btn_ok = lv_button_create(top_bar);
  lv_obj_set_size(btn_ok, btn_top_w, top_h - 4);
  lv_obj_align(btn_ok, LV_ALIGN_RIGHT_MID, -(btn_top_w + 4), 0);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x16A34A), 0);
  lv_obj_set_style_radius(btn_ok, 3, 0);
  lv_obj_set_style_pad_all(btn_ok, 0, 0);
  lv_obj_add_event_cb(btn_ok, term_edit_btn_run_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_ok = lv_label_create(btn_ok);
  lv_label_set_text(lbl_ok, is_large ? "▶ Run" : "Run");
  lv_obj_set_style_text_color(lbl_ok, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_ok, LV_ALIGN_CENTER, 0, 0);

  // Textarea for command input
  int32_t ta_y = top_h + 3;
  int32_t ta_h = is_large ? 38 : 28;
  st->edit_ta = lv_textarea_create(st->edit_overlay);
  lv_obj_set_size(st->edit_ta, (int32_t)w - 8, ta_h);
  lv_obj_align(st->edit_ta, LV_ALIGN_TOP_MID, 0, ta_y);
  lv_textarea_set_one_line(st->edit_ta, true);
  const char *initial_cmd =
      st->active_cmd[0] ? st->active_cmd : s_preset_cmds[st->preset_idx];
  lv_textarea_set_text(st->edit_ta, initial_cmd);
  lv_textarea_set_cursor_pos(st->edit_ta, LV_TEXTAREA_CURSOR_LAST);
  lv_obj_set_style_bg_color(st->edit_ta, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_text_color(st->edit_ta, lv_color_hex(0xFDE047), 0);
  lv_obj_set_style_border_color(st->edit_ta, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_border_width(st->edit_ta, 1, 0);
  lv_obj_set_style_radius(st->edit_ta, 3, 0);
  lv_obj_set_style_pad_all(st->edit_ta, 4, 0);

  // Blinking cursor matches text displayment color (0xFDE047)
  lv_obj_set_style_bg_color(st->edit_ta, lv_color_hex(0xFDE047), LV_PART_CURSOR);
  lv_obj_set_style_bg_opa(st->edit_ta, LV_OPA_COVER, LV_PART_CURSOR);
  lv_obj_set_style_border_color(st->edit_ta, lv_color_hex(0xFDE047), LV_PART_CURSOR);
  lv_obj_set_style_border_width(st->edit_ta, 0, LV_PART_CURSOR);
  lv_obj_set_style_text_color(st->edit_ta, lv_color_hex(0x0F172A), LV_PART_CURSOR);

  // Hint label between textarea and keyboard
  if (is_large) {
    lv_obj_t *hint = lv_label_create(st->edit_overlay);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, ta_y + ta_h + 4);
    lv_label_set_text(hint, "Touch keys to type  •  [▶ Run] to execute  •  [✕ Cancel] to discard");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x64748B), 0);
  }

  // On-screen Virtual Keyboard
  // Height deduced proportionally:
  // On large displays (e.g. 800x480 P4): cap to ~45% screen height (~25% width, ~200px)
  // On compact displays (e.g. 240x280 Lilka): allocate ~52% screen height (~145px) for clear legibility
  int32_t kb_y = ta_y + ta_h + 3;
  int32_t kb_h;
  if (is_large) {
    int32_t max_kb_h = (int32_t)(h * 0.45f);
    kb_h = (int32_t)(w * 0.25f);
    if (kb_h > max_kb_h) kb_h = max_kb_h;
    if (kb_h < 150) kb_h = 150;
  } else {
    kb_h = (int32_t)(h * 0.52f);
  }
  if (kb_h > (int32_t)h - kb_y - 4) kb_h = (int32_t)h - kb_y - 4;

  st->edit_kb = lv_keyboard_create(st->edit_overlay);
  lv_obj_set_size(st->edit_kb, (int32_t)w - 4, kb_h);
  lv_obj_align(st->edit_kb, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_keyboard_set_textarea(st->edit_kb, st->edit_ta);
  lv_obj_add_event_cb(st->edit_kb, term_kb_event_cb, LV_EVENT_ALL, st);

  // Styling keyboard for visibility on all screen resolutions
  lv_obj_set_style_bg_color(st->edit_kb, lv_color_hex(0x0F172A), LV_PART_MAIN);
  lv_obj_set_style_border_color(st->edit_kb, lv_color_hex(0x334155), LV_PART_MAIN);
  lv_obj_set_style_border_width(st->edit_kb, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(st->edit_kb, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(st->edit_kb, 2, LV_PART_MAIN);

  // Normal keyboard buttons
  lv_obj_set_style_bg_color(st->edit_kb, lv_color_hex(0x1E293B), LV_PART_ITEMS);
  lv_obj_set_style_text_color(st->edit_kb, lv_color_hex(0xF8FAFC), LV_PART_ITEMS);
  lv_obj_set_style_border_color(st->edit_kb, lv_color_hex(0x475569), LV_PART_ITEMS);
  lv_obj_set_style_border_width(st->edit_kb, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(st->edit_kb, 3, LV_PART_ITEMS);

  // Focused/Selected button styling
  lv_obj_set_style_bg_color(st->edit_kb, lv_color_hex(0x2563EB),
                             LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(st->edit_kb, lv_color_hex(0xFACC15),
                                LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(st->edit_kb, 2,
                                LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(st->edit_kb, lv_color_hex(0xFFFFFF),
                               LV_PART_ITEMS | LV_STATE_FOCUSED);

  lv_obj_set_style_bg_color(st->edit_kb, lv_color_hex(0x2563EB),
                             LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_color(st->edit_kb, lv_color_hex(0xFACC15),
                                LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_width(st->edit_kb, 2,
                                LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_text_color(st->edit_kb, lv_color_hex(0xFFFFFF),
                               LV_PART_ITEMS | LV_STATE_FOCUS_KEY);

  lv_group_t *g = eos_lvgl_get_group();
  if (g) {
    lv_group_add_obj(g, st->edit_kb);
    lv_group_focus_obj(st->edit_kb);
  }
  lv_obj_add_state(st->edit_kb, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
  lv_buttonmatrix_set_selected_button(st->edit_kb, 0);
}

static char term_key_to_ascii(uint16_t key) {
  if (key >= KEY_1 && key <= KEY_9)
    return (char)('1' + (key - KEY_1));
  if (key == KEY_0)
    return '0';
  if (key >= KEY_A && key <= KEY_Z)
    return (char)('a' + (key - KEY_A));
  if (key == KEY_SPACE)
    return ' ';
  if (key == KEY_MINUS)
    return '-';
  if (key == KEY_SLASH)
    return '/';
  if (key == KEY_DOT)
    return '.';
  return 0;
}

static void term_handle_key(term_viewer_state_t *st, uint16_t key) {
  if (st->is_editing) {
    if (key == KEY_B || key == KEY_ESC) {
      term_close_edit_mode(st);
      return;
    }
    if (key == KEY_BACKSPACE) {
      if (st->edit_ta) {
        lv_textarea_delete_char(st->edit_ta);
      }
      return;
    }
    if (key == KEY_ENTER) {
      if (st->edit_ta) {
        const char *text = lv_textarea_get_text(st->edit_ta);
        if (text) {
          while (*text && isspace((unsigned char)*text)) text++;
          size_t len = strlen(text);
          while (len > 0 && isspace((unsigned char)text[len - 1])) len--;
          if (len > 0) {
            if (len >= sizeof(st->active_cmd)) len = sizeof(st->active_cmd) - 1;
            memcpy(st->active_cmd, text, len);
            st->active_cmd[len] = '\0';
          }
        }
      }
      term_close_edit_mode(st);
      st->trigger_run = true;
      return;
    }

    if (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT) {
      if (st->edit_kb) {
        if (lv_buttonmatrix_get_selected_button(st->edit_kb) ==
            LV_BUTTONMATRIX_BUTTON_NONE) {
          lv_buttonmatrix_set_selected_button(st->edit_kb, 0);
        }
        uint32_t lv_k = (key == KEY_RIGHT) ? LV_KEY_RIGHT :
                        (key == KEY_LEFT)  ? LV_KEY_LEFT :
                        (key == KEY_DOWN)  ? LV_KEY_DOWN : LV_KEY_UP;
        lv_obj_send_event(st->edit_kb, LV_EVENT_KEY, &lv_k);
      }
      return;
    }

    if (key == KEY_A) {
      if (st->edit_kb) {
        uint32_t sel = lv_buttonmatrix_get_selected_button(st->edit_kb);
        if (sel == LV_BUTTONMATRIX_BUTTON_NONE) {
          sel = 0;
          lv_buttonmatrix_set_selected_button(st->edit_kb, 0);
        }
        lv_obj_send_event(st->edit_kb, LV_EVENT_VALUE_CHANGED, &sel);
      }
      return;
    }

    char ch = term_key_to_ascii(key);
    if (ch != 0 && st->edit_ta) {
      lv_textarea_add_char(st->edit_ta, (uint32_t)ch);
      return;
    }
    return;
  }

  switch (key) {
  case KEY_UP:
    term_scroll_line_up(st);
    break;

  case KEY_DOWN:
    term_scroll_line_down(st);
    break;

  case KEY_PAGEUP:
    term_scroll_page_up(st);
    break;

  case KEY_PAGEDOWN:
  case KEY_SPACE:
    term_scroll_page_down(st);
    break;

  case KEY_D:
  case KEY_E:
    term_open_edit_mode(st);
    break;

  case KEY_LEFT:
    st->preset_idx =
        (st->preset_idx - 1 + TERM_PRESET_COUNT) % TERM_PRESET_COUNT;
    snprintf(st->active_cmd, sizeof(st->active_cmd), "%s",
             s_preset_cmds[st->preset_idx]);
    term_update_ui(st);
    break;

  case KEY_RIGHT:
    st->preset_idx = (st->preset_idx + 1) % TERM_PRESET_COUNT;
    snprintf(st->active_cmd, sizeof(st->active_cmd), "%s",
             s_preset_cmds[st->preset_idx]);
    term_update_ui(st);
    break;

  case KEY_C:
    eos_lvgl_set_full_fallback(!eos_lvgl_is_full_fallback());
    term_update_ui(st);
    break;

  case KEY_A:
  case KEY_ENTER:
    st->trigger_run = true;
    break;

  case KEY_B:
  case KEY_ESC:
    st->should_exit = true;
    break;

  default:
    break;
  }
}

int lvgl_term_main(int argc, char **argv) {
  term_viewer_state_t *st = calloc(1, sizeof(term_viewer_state_t));
  if (!st) {
    perror("lvgl_term calloc");
    return -1;
  }

  st->text_buf = calloc(1, TERM_MAX_TEXT_BYTES);
  if (!st->text_buf) {
    free(st);
    return -1;
  }

  st->start_time_ms = get_time_ms();
  snprintf(st->file_path, sizeof(st->file_path), "%s", TERM_DEFAULT_OUT_FILE);
  snprintf(st->title, sizeof(st->title), "Terminal Viewer");
  snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", s_preset_cmds[0]);

  bool run_cmd_arg = false;
  bool explicit_file = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage:\n");
      printf("  <cmd> | lvgl_term\n");
      printf("  <cmd> > /term.out && lvgl_term /term.out [title] [cmd]\n");
      printf("  lvgl_term -c \"<cmd>\" [-t <timeout_ms>]\n");
      printf("  lvgl_term -t <timeout_ms>\n");
      free(st->text_buf);
      free(st);
      return 0;
    } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
      snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", argv[++i]);
      run_cmd_arg = true;
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      st->timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (argv[i][0] != '-') {
      if (!explicit_file) {
        snprintf(st->file_path, sizeof(st->file_path), "%s", argv[i]);
        snprintf(st->title, sizeof(st->title), "Term: %.22s", argv[i]);
        explicit_file = true;
      } else if (!st->has_exit_code && argv[i][0]) {
        snprintf(st->title, sizeof(st->title), "%s", argv[i]);
      }
    }
  }

  // Sync preset index if active_cmd matches a preset
  for (int i = 0; i < TERM_PRESET_COUNT; i++) {
    if (strcmp(st->active_cmd, s_preset_cmds[i]) == 0) {
      st->preset_idx = i;
      break;
    }
  }

  // Check if stdin is redirected from a pipe or file (`cmd | lvgl_term`)
  eos_proc_t *proc = eos_proc_current();
  bool stdin_redirected = (proc && eos_proc_fd_map_get(proc, STDIN_FILENO) >= 0);

  // Initialize LVGL display
  eos_lvgl_cfg_t cfg = {
      .fb_path = NULL,
      .kbd_path = NULL,
      .partial_lines = 0,
      .force_full_frame = false,
      .dpad_group_nav = false,
      .enable_keyboard = true,
  };

  if (eos_lvgl_init(&cfg) != 0) {
    // CLI fallback when display is occupied or unavailable
    if (run_cmd_arg) {
      char full_cmd[EOS_LARGE_STR_LEN];
      snprintf(full_cmd, sizeof(full_cmd), "%s", st->active_cmd);
      system(full_cmd);
    } else if (stdin_redirected && !explicit_file) {
      char chunk[512];
      ssize_t n;
      while ((n = read(STDIN_FILENO, chunk, sizeof(chunk))) > 0) {
        (void)write(STDOUT_FILENO, chunk, (size_t)n);
      }
    } else if (explicit_file) {
      int fd = open(st->file_path, O_RDONLY);
      if (fd >= 0) {
        char chunk[512];
        ssize_t n;
        while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
          (void)write(STDOUT_FILENO, chunk, (size_t)n);
        }
        close(fd);
      }
    }
    free(st->text_buf);
    free(st);
    return 0;
  }

  eos_lvgl_clear_keys();
  term_build_ui(st);

  if (run_cmd_arg) {
    term_execute_cmd_to_file(st, st->active_cmd);
  } else if (stdin_redirected && !explicit_file) {
    snprintf(st->title, sizeof(st->title), "Piped Stream Output");
    term_load_from_fd(st, STDIN_FILENO, TERM_DEFAULT_OUT_FILE);
  } else if (explicit_file) {
    term_load_from_file(st, st->file_path);
  } else {
    // Default initial execution of first preset
    term_execute_cmd_to_file(st, st->active_cmd);
  }

  term_update_ui(st);

  // Main UI event and rendering loop
  while (!st->should_exit) {
    if (st->timeout_ms > 0) {
      uint64_t elapsed_ms = get_time_ms() - st->start_time_ms;
      if (elapsed_ms >= st->timeout_ms) {
        break;
      }
    }

    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      term_handle_key(st, key);
      if (st->should_exit || st->trigger_run)
        break;
    }

    if (st->trigger_run) {
      st->trigger_run = false;
      term_execute_cmd_to_file(st, st->active_cmd);
      if (st->viewport) {
        lv_obj_scroll_to_y(st->viewport, 0, LV_ANIM_OFF);
      }
      eos_lvgl_clear_keys();
      term_update_ui(st);
      continue;
    }

    eos_lvgl_step(10);
  }

  eos_lvgl_deinit();
  free(st->text_buf);
  free(st);
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_term_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_term",
    .name = "Terminal Viewer",
    .group = "Tools",
    .description = "Terminal runner and viewer with on-screen keyboard",
    .entry_point = lvgl_term_main,
};
