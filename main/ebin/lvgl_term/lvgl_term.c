#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TERM_DEFAULT_OUT_FILE "/term.out"
#define TERM_DEFAULT_IN_FILE  "/term.in"
#define TERM_MAX_TEXT_BYTES   16384
#define TERM_SCROLL_STEP_PX   28
#define TERM_PAGE_STEP_PX     120

static const char *const s_preset_cmds[] = {
    "sysinfo",
    "cat /eos.log",
    "env",
    "tree /",
    "ls /bin",
    "ls /dev",
    "heapdump",
    "fsdump",
    "fddump",
    "iodump",
    "intrdump",
    "pwd",
    "test_pthread",
    "test_rtos_tasks",
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

  lv_obj_t *hdr_title;
  lv_obj_t *hdr_meta;
  lv_obj_t *cmd_label;
  lv_obj_t *viewport;
  lv_obj_t *out_label;
  lv_obj_t *footer_label;

  lv_obj_t *edit_overlay;
  lv_obj_t *edit_ta;
  lv_obj_t *edit_kb;
} term_viewer_state_t;

static void ensure_empty_input_file(void) {
  int fd = open(TERM_DEFAULT_IN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    close(fd);
  }
}

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

    // Strip ANSI escape sequences: ESC '[' ... <letter>
    if (ch == 0x1B) {
      r++;
      if (r < raw_len && raw[r] == '[') {
        r++;
        while (r < raw_len) {
          unsigned char c2 = (unsigned char)raw[r++];
          if ((c2 >= 'A' && c2 <= 'Z') || (c2 >= 'a' && c2 <= 'z') ||
              c2 == 'm' || c2 == 'K' || c2 == 'J' || c2 == 'H') {
            break;
          }
        }
      }
      continue;
    }

    // Convert UTF-8 box-drawing sequences (0xE2 0x94 0x80..0xBF) used by `tree`
    if (ch == 0xE2 && r + 2 < raw_len && (unsigned char)raw[r + 1] == 0x94) {
      unsigned char b3 = (unsigned char)raw[r + 2];
      char repl = '-';
      if (b3 == 0x82) {
        repl = '|'; // │
      } else if (b3 == 0x9C) {
        repl = '+'; // ├
      } else if (b3 == 0x94) {
        repl = '`'; // └
      } else if (b3 == 0x80) {
        repl = '-'; // ─
      }
      dst[w++] = repl;
      r += 3;
      continue;
    }

    // Skip other multi-byte UTF-8 sequences that aren't in standard ASCII font
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
             "No output file at '%s'.\nPress A/ENTER to run '%s'.", path,
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
  snprintf(full_cmd, sizeof(full_cmd), "%s < %s > %s 2>&1", cmd,
           TERM_DEFAULT_IN_FILE, out_path);

  int rc = system(full_cmd);
  st->last_exit_code = rc;
  st->has_exit_code = true;
  snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", cmd);
  snprintf(st->title, sizeof(st->title), "%.18s (exit %d)", cmd, rc);

  term_load_from_file(st, out_path);
  return rc;
}

static void term_update_ui(term_viewer_state_t *st) {
  if (!st->hdr_title)
    return;

  lv_label_set_text(st->hdr_title,
                    st->title[0] ? st->title : "Terminal Viewer");

  if (st->hdr_meta) {
    int32_t sy = st->viewport ? lv_obj_get_scroll_y(st->viewport) : 0;
    if (sy < 0)
      sy = 0;
    char mbuf[48];
    snprintf(mbuf, sizeof(mbuf), "%dL %s", st->total_lines,
             eos_lvgl_is_full_fallback() ? "FULL" : "PART");
    lv_label_set_text(st->hdr_meta, mbuf);
  }

  if (st->cmd_label) {
    const char *cmd =
        st->active_cmd[0] ? st->active_cmd : s_preset_cmds[st->preset_idx];
    char cbuf[96];
    snprintf(cbuf, sizeof(cbuf), "< Cmd: %.40s >  [A:Run D:Edit]", cmd);
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
             "U/D:Scrl(%d/%d) L/R:Preset A:Run D:Edit B:Exit", (int)sy,
             (int)(sy + (bottom > 0 ? bottom : 0)));
    lv_label_set_text(st->footer_label, fbuf);
  }
}

static void term_build_ui(term_viewer_state_t *st) {
  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1120), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Header bar (y=0, h=22)
  lv_obj_t *hdr = lv_obj_create(scr);
  lv_obj_set_size(hdr, (int32_t)w, 22);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1E3A8A), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_hor(hdr, 6, 0);
  lv_obj_set_style_pad_ver(hdr, 2, 0);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  st->hdr_title = lv_label_create(hdr);
  lv_obj_set_width(st->hdr_title, (int32_t)w - 86);
  lv_label_set_long_mode(st->hdr_title, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->hdr_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(st->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);

  st->hdr_meta = lv_label_create(hdr);
  lv_obj_set_style_text_color(st->hdr_meta, lv_color_hex(0x34D399), 0);
  lv_obj_align(st->hdr_meta, LV_ALIGN_RIGHT_MID, 0, 0);

  // 2. Command selector bar (y=22, h=20)
  lv_obj_t *cbar = lv_obj_create(scr);
  lv_obj_set_size(cbar, (int32_t)w, 20);
  lv_obj_align(cbar, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_set_style_bg_color(cbar, lv_color_hex(0x172554), 0);
  lv_obj_set_style_border_width(cbar, 0, 0);
  lv_obj_set_style_radius(cbar, 0, 0);
  lv_obj_set_style_pad_hor(cbar, 6, 0);
  lv_obj_set_style_pad_ver(cbar, 1, 0);
  lv_obj_clear_flag(cbar, LV_OBJ_FLAG_SCROLLABLE);

  st->cmd_label = lv_label_create(cbar);
  lv_obj_set_width(st->cmd_label, (int32_t)w - 12);
  lv_label_set_long_mode(st->cmd_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->cmd_label, lv_color_hex(0xFDE047), 0);
  lv_obj_align(st->cmd_label, LV_ALIGN_LEFT_MID, 0, 0);

  // 3. Scrollable terminal output viewport (y=44, h = h - 66)
  int32_t vp_h = (int32_t)h - 66;
  if (vp_h < 60)
    vp_h = 60;

  st->viewport = lv_obj_create(scr);
  lv_obj_set_size(st->viewport, (int32_t)w - 4, vp_h);
  lv_obj_align(st->viewport, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_bg_color(st->viewport, lv_color_hex(0x050811), 0);
  lv_obj_set_style_border_color(st->viewport, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(st->viewport, 1, 0);
  lv_obj_set_style_radius(st->viewport, 4, 0);
  lv_obj_set_style_pad_all(st->viewport, 5, 0);
  lv_obj_set_scroll_dir(st->viewport, LV_DIR_VER);

  st->out_label = lv_label_create(st->viewport);
  lv_obj_set_width(st->out_label, (int32_t)w - 18);
  lv_label_set_long_mode(st->out_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(st->out_label, lv_color_hex(0x4ADE80), 0);
  lv_obj_align(st->out_label, LV_ALIGN_TOP_LEFT, 0, 0);

  // 4. Footer bar (bottom 20px)
  lv_obj_t *ftr = lv_obj_create(scr);
  lv_obj_set_size(ftr, (int32_t)w, 20);
  lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(ftr, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_width(ftr, 0, 0);
  lv_obj_set_style_radius(ftr, 0, 0);
  lv_obj_set_style_pad_hor(ftr, 6, 0);
  lv_obj_set_style_pad_ver(ftr, 1, 0);
  lv_obj_clear_flag(ftr, LV_OBJ_FLAG_SCROLLABLE);

  st->footer_label = lv_label_create(ftr);
  lv_obj_set_width(st->footer_label, (int32_t)w - 12);
  lv_label_set_long_mode(st->footer_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->footer_label, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(st->footer_label, LV_ALIGN_LEFT_MID, 0, 0);

  term_update_ui(st);
}

static void term_scroll_by(term_viewer_state_t *st, int32_t dy) {
  if (!st->viewport)
    return;

  if (dy > 0) {
    // Scroll down (content moves up)
    int32_t bottom = lv_obj_get_scroll_bottom(st->viewport);
    if (bottom <= 0)
      return;
    if (dy > bottom)
      dy = bottom;
    lv_obj_scroll_by(st->viewport, 0, -dy, LV_ANIM_OFF);
  } else if (dy < 0) {
    // Scroll up (content moves down)
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

static void term_close_edit_mode(term_viewer_state_t *st);

static void term_kb_event_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_READY) {
    if (st->edit_ta) {
      const char *text = lv_textarea_get_text(st->edit_ta);
      if (text && text[0]) {
        snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", text);
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

  if (st->edit_overlay) {
    lv_obj_delete(st->edit_overlay);
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

  lv_obj_t *scr = lv_screen_active();

  // Full-screen overlay container
  st->edit_overlay = lv_obj_create(scr);
  lv_obj_set_size(st->edit_overlay, (int32_t)w, (int32_t)h);
  lv_obj_align(st->edit_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(st->edit_overlay, lv_color_hex(0x050811), 0);
  lv_obj_set_style_bg_opa(st->edit_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(st->edit_overlay, 0, 0);
  lv_obj_set_style_radius(st->edit_overlay, 0, 0);
  lv_obj_set_style_pad_all(st->edit_overlay, 3, 0);
  lv_obj_clear_flag(st->edit_overlay, LV_OBJ_FLAG_SCROLLABLE);

  // Title bar
  lv_obj_t *title = lv_label_create(st->edit_overlay);
  lv_label_set_text(title, "Edit Command  [Sel:Run  B:Cancel]");
  lv_obj_set_style_text_color(title, lv_color_hex(0x60A5FA), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 2, 0);

  // Single-line Textarea for command input
  st->edit_ta = lv_textarea_create(st->edit_overlay);
  lv_obj_set_size(st->edit_ta, (int32_t)w - 8, 28);
  lv_obj_align(st->edit_ta, LV_ALIGN_TOP_MID, 0, 18);
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

  // Helper hints bar
  lv_obj_t *hint = lv_label_create(st->edit_overlay);
  lv_label_set_text(hint, "D-Pad:Move A:Type C:Del Spc:Space D:Mode");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 2, 48);

  // Virtual keyboard below hints
  int32_t kb_y = 66;
  int32_t kb_h = (int32_t)h - kb_y - 2;
  if (kb_h < 100)
    kb_h = 100;
  st->edit_kb = lv_keyboard_create(st->edit_overlay);
  lv_obj_set_size(st->edit_kb, (int32_t)w - 4, kb_h);
  lv_obj_align(st->edit_kb, LV_ALIGN_TOP_MID, 0, kb_y);
  lv_keyboard_set_textarea(st->edit_kb, st->edit_ta);
  lv_obj_add_event_cb(st->edit_kb, term_kb_event_cb, LV_EVENT_ALL, st);

  // Styling keyboard for visibility on 280x240 display
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

  // Focused/Selected button styling (bright blue bg with yellow border)
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
  if (key == KEY_SPACE)
    return ' ';
  if (key == KEY_MINUS)
    return '-';
  if (key == KEY_EQUAL)
    return '=';
  if (key == KEY_SLASH)
    return '/';
  if (key == KEY_DOT)
    return '.';
  if (key == KEY_COMMA)
    return ',';
  if (key == KEY_SEMICOLON)
    return ';';
  if (key == KEY_APOSTROPHE)
    return '\'';
  if (key == KEY_GRAVE)
    return '`';
  if (key == KEY_BACKSLASH)
    return '\\';

  switch (key) {
  case KEY_Q: return 'q';
  case KEY_W: return 'w';
  case KEY_E: return 'e';
  case KEY_R: return 'r';
  case KEY_T: return 't';
  case KEY_Y: return 'y';
  case KEY_U: return 'u';
  case KEY_I: return 'i';
  case KEY_O: return 'o';
  case KEY_P: return 'p';
  case KEY_A: return 'a';
  case KEY_S: return 's';
  case KEY_D: return 'd';
  case KEY_F: return 'f';
  case KEY_G: return 'g';
  case KEY_H: return 'h';
  case KEY_J: return 'j';
  case KEY_K: return 'k';
  case KEY_L: return 'l';
  case KEY_Z: return 'z';
  case KEY_X: return 'x';
  case KEY_C: return 'c';
  case KEY_V: return 'v';
  case KEY_B: return 'b';
  case KEY_N: return 'n';
  case KEY_M: return 'm';
  default:
    return 0;
  }
}

static void term_handle_key(term_viewer_state_t *st, uint16_t key) {
  if (st->is_editing) {
    if (key == KEY_B || key == KEY_ESC) {
      term_close_edit_mode(st);
      return;
    }

    if (key == KEY_ENTER) {
      if (st->edit_ta) {
        const char *text = lv_textarea_get_text(st->edit_ta);
        if (text && text[0]) {
          snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", text);
        }
      }
      term_close_edit_mode(st);
      st->trigger_run = true;
      return;
    }

    if (key == KEY_C || key == KEY_BACKSPACE || key == KEY_DELETE) {
      if (st->edit_ta) {
        lv_textarea_delete_char(st->edit_ta);
      }
      return;
    }

    if (key == KEY_SPACE) {
      if (st->edit_ta) {
        lv_textarea_add_char(st->edit_ta, ' ');
      }
      return;
    }

    if (key == KEY_D) {
      if (st->edit_kb) {
        lv_keyboard_mode_t cur_mode = lv_keyboard_get_mode(st->edit_kb);
        lv_keyboard_mode_t next_mode;
        switch (cur_mode) {
        case LV_KEYBOARD_MODE_TEXT_LOWER:
          next_mode = LV_KEYBOARD_MODE_TEXT_UPPER;
          break;
        case LV_KEYBOARD_MODE_TEXT_UPPER:
          next_mode = LV_KEYBOARD_MODE_SPECIAL;
          break;
        case LV_KEYBOARD_MODE_SPECIAL:
          next_mode = LV_KEYBOARD_MODE_NUMBER;
          break;
        case LV_KEYBOARD_MODE_NUMBER:
        default:
          next_mode = LV_KEYBOARD_MODE_TEXT_LOWER;
          break;
        }
        lv_keyboard_set_mode(st->edit_kb, next_mode);
        if (lv_buttonmatrix_get_selected_button(st->edit_kb) ==
            LV_BUTTONMATRIX_BUTTON_NONE) {
          lv_buttonmatrix_set_selected_button(st->edit_kb, 0);
        }
      }
      return;
    }

    if (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT) {
      if (st->edit_kb) {
        uint32_t lv_k = (key == KEY_UP)    ? LV_KEY_UP
                        : (key == KEY_DOWN) ? LV_KEY_DOWN
                        : (key == KEY_LEFT) ? LV_KEY_LEFT
                                            : LV_KEY_RIGHT;
        lv_obj_send_event(st->edit_kb, LV_EVENT_KEY, &lv_k);
      }
      return;
    }

    if (key == KEY_A) {
      if (st->edit_kb) {
        uint32_t btn_id = lv_buttonmatrix_get_selected_button(st->edit_kb);
        if (btn_id == LV_BUTTONMATRIX_BUTTON_NONE) {
          btn_id = 0;
          lv_buttonmatrix_set_selected_button(st->edit_kb, 0);
        }
        lv_obj_send_event(st->edit_kb, LV_EVENT_VALUE_CHANGED, &btn_id);
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
    term_scroll_by(st, -TERM_SCROLL_STEP_PX);
    break;

  case KEY_DOWN:
    term_scroll_by(st, TERM_SCROLL_STEP_PX);
    break;

  case KEY_SPACE:
    term_scroll_by(st, TERM_PAGE_STEP_PX);
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

  snprintf(st->file_path, sizeof(st->file_path), "%s", TERM_DEFAULT_OUT_FILE);
  snprintf(st->title, sizeof(st->title), "Terminal Output");
  snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", s_preset_cmds[0]);

  bool run_cmd_arg = false;
  bool explicit_file = false;

  if (argc > 1) {
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      printf("Usage:\n");
      printf("  <cmd> | lvgl_term\n");
      printf("  <cmd> > /term.out && lvgl_term /term.out [title] [cmd]\n");
      printf("  lvgl_term -c \"<cmd>\"\n");
      free(st->text_buf);
      free(st);
      return 0;
    } else if (strcmp(argv[1], "-c") == 0 && argc > 2) {
      snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", argv[2]);
      run_cmd_arg = true;
    } else {
      snprintf(st->file_path, sizeof(st->file_path), "%s", argv[1]);
      snprintf(st->title, sizeof(st->title), "Term: %.22s", argv[1]);
      explicit_file = true;
      if (argc > 2 && argv[2][0]) {
        snprintf(st->title, sizeof(st->title), "%s", argv[2]);
      }
      if (argc > 3 && argv[3][0]) {
        snprintf(st->active_cmd, sizeof(st->active_cmd), "%s", argv[3]);
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
  eos_tctx_t *tctx = eos_tctx_get();
  bool stdin_redirected = (tctx && tctx->fd_map[STDIN_FILENO] >= 0);

  if (run_cmd_arg) {
    term_execute_cmd_to_file(st, st->active_cmd);
  } else if (stdin_redirected && !explicit_file) {
    snprintf(st->title, sizeof(st->title), "Piped Terminal Output");
    term_load_from_fd(st, STDIN_FILENO, st->file_path);
  } else {
    if (term_load_from_file(st, st->file_path) < 0 && !explicit_file) {
      // No output file yet on standalone launch — run default `sysinfo` task
      term_execute_cmd_to_file(st, st->active_cmd);
    }
  }

  eos_lvgl_cfg_t cfg = {
      .fb_path = NULL,
      .kbd_path = NULL,
      .partial_lines = 0,
      .force_full_frame = false,
      .dpad_group_nav = false,
      .enable_keyboard = true,
  };

  if (eos_lvgl_init(&cfg) != 0) {
    printf("%s\n", st->text_buf);
    free(st->text_buf);
    free(st);
    return -1;
  }

  eos_lvgl_clear_keys();
  term_build_ui(st);

  while (!st->should_exit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      term_handle_key(st, key);
      if (st->should_exit || st->trigger_run)
        break;
    }

    if (st->trigger_run) {
      st->trigger_run = false;
      if (st->footer_label) {
        char rmsg[64];
        snprintf(rmsg, sizeof(rmsg), "Running '%.40s'...", st->active_cmd);
        lv_label_set_text(st->footer_label, rmsg);
        if (eos_lvgl_get_display()) {
          lv_refr_now(eos_lvgl_get_display());
        }
      }
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
    .description = "Interactive terminal viewer and runner with on-screen command editor",
    .entry_point = lvgl_term_main,
};
