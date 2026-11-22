#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ecore/proc.h"
#include "emisc/fancymacro.h"
#include "ebin/lvgl_term/lvgl_term.h"

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

typedef struct {
  term_logic_t logic;
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

  lv_obj_t *edit_dialog;
} term_viewer_state_t;

static uint64_t get_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * (uint64_t)EOS_MS_PER_SEC + (uint64_t)tv.tv_usec / (uint64_t)EOS_US_PER_MS;
}

// Forward declarations
static void term_update_ui(term_viewer_state_t *st);
static void term_open_edit_mode(term_viewer_state_t *st);
static void term_close_edit_mode(term_viewer_state_t *st);

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
                    st->logic.title[0] ? st->logic.title : "Terminal Viewer");

  if (st->hdr_meta) {
    char mbuf[EOS_BADGE_STR_LEN];
    snprintf(mbuf, sizeof(mbuf), "%d lines", st->logic.total_lines);
    lv_label_set_text(st->hdr_meta, mbuf);
  }

  if (st->cmd_label) {
    char cbuf[EOS_HEADER_STR_LEN];
    snprintf(cbuf, sizeof(cbuf), "%.48s", st->logic.active_cmd);
    lv_label_set_text(st->cmd_label, cbuf);
  }

  if (st->out_label && st->logic.text_buf) {
    lv_label_set_text(st->out_label, st->logic.text_buf);
  }

  if (st->footer_label) {
    int32_t sy = st->viewport ? lv_obj_get_scroll_y(st->viewport) : 0;
    int32_t bottom = st->viewport ? lv_obj_get_scroll_bottom(st->viewport) : 0;
    char fbuf[EOS_HEADER_STR_LEN];
    snprintf(fbuf, sizeof(fbuf),
             "U/D:Scrl(%d/%d)  L/R:Preset  A:Run  D/KB:Edit  B:Exit", (int)sy,
             (int)(sy + (bottom > 0 ? bottom : 0)));
    lv_label_set_text(st->footer_label, fbuf);
  }

  FILE *tb = fopen("/tmp/tbar.tmp", "w");
  if (tb) {
    fprintf(tb, "@%ld Term: %s\n[▲▼]Scroll  [◄►]Preset  [A]Run  [D]Edit  [B]Exit\n",
            (long)getpid(), st->logic.active_cmd);
    fclose(tb);
    rename("/tmp/tbar.tmp", "/tmp/tbar");
  }
}

// Touch button event handlers
static void term_btn_prev_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  term_logic_prev_preset(&st->logic);
  term_update_ui(st);
}

static void term_btn_next_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  term_logic_next_preset(&st->logic);
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

static void term_back_btn_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  if (st) {
    st->should_exit = true;
  }
}

static void term_cmd_box_cb(lv_event_t *e) {
  term_viewer_state_t *st = (term_viewer_state_t *)lv_event_get_user_data(e);
  term_open_edit_mode(st);
}

static void term_build_header(term_viewer_state_t *st, lv_obj_t *scr,
                               uint32_t w, bool is_large, int32_t hdr_h) {
  lv_obj_t *hdr = lv_obj_create(scr);
  lv_obj_set_size(hdr, (int32_t)w, hdr_h);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(EOS_COLOR_BG_HDR), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_hor(hdr, 6, 0);
  lv_obj_set_style_pad_ver(hdr, is_large ? 4 : 2, 0);

  st->hdr_title = lv_label_create(hdr);
  lv_obj_set_width(st->hdr_title, (int32_t)w - (is_large ? TERM_HDR_TITLE_MARGIN_LG : TERM_HDR_TITLE_MARGIN_SM));
  lv_label_set_long_mode(st->hdr_title, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->hdr_title, lv_color_hex(EOS_THEME_TEXT_WHITE), 0);
  lv_obj_align(st->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);

  st->hdr_meta = lv_label_create(hdr);
  lv_obj_set_style_text_color(st->hdr_meta, lv_color_hex(EOS_THEME_ACCENT_GREEN), 0);
  lv_obj_align(st->hdr_meta, LV_ALIGN_RIGHT_MID,
               is_large ? -TERM_HDR_META_OFFSET_LG : -TERM_HDR_META_OFFSET_SM, 0);

  // Dedicated touch exit button
  lv_obj_t *back_btn = eos_lvgl_create_back_btn(hdr, term_back_btn_cb, st);
  if (back_btn) {
    lv_obj_set_size(back_btn, is_large ? 28 : 22, is_large ? 22 : 16);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, -2, 0);
  }
}

static void term_build_cbar(term_viewer_state_t *st, lv_obj_t *scr, uint32_t w,
                            bool is_large, int32_t hdr_h, int32_t cbar_h) {
  lv_obj_t *cbar = lv_obj_create(scr);
  lv_obj_set_size(cbar, (int32_t)w, cbar_h);
  lv_obj_align(cbar, LV_ALIGN_TOP_MID, 0, hdr_h);
  lv_obj_set_style_bg_color(cbar, lv_color_hex(EOS_COLOR_BG_SUBHDR), 0);
  lv_obj_set_style_border_width(cbar, 0, 0);
  lv_obj_set_style_radius(cbar, 0, 0);
  lv_obj_set_style_pad_hor(cbar, 4, 0);
  lv_obj_set_style_pad_ver(cbar, 2, 0);

  int32_t nav_w = is_large ? 34 : 22;
  int32_t kb_w = is_large ? 86 : 32;
  int32_t run_w = is_large ? 74 : 28;
  int32_t btn_h = cbar_h - 4;

  st->btn_prev = lv_button_create(cbar);
  lv_obj_set_size(st->btn_prev, nav_w, btn_h);
  lv_obj_align(st->btn_prev, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(st->btn_prev, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
  lv_obj_set_style_radius(st->btn_prev, 3, 0);
  lv_obj_set_style_pad_all(st->btn_prev, 0, 0);
  lv_obj_add_event_cb(st->btn_prev, term_btn_prev_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_prev = lv_label_create(st->btn_prev);
  lv_label_set_text(lbl_prev, "<");
  lv_obj_align(lbl_prev, LV_ALIGN_CENTER, 0, 0);

  st->btn_next = lv_button_create(cbar);
  lv_obj_set_size(st->btn_next, nav_w, btn_h);
  lv_obj_align(st->btn_next, LV_ALIGN_LEFT_MID, nav_w + 3, 0);
  lv_obj_set_style_bg_color(st->btn_next, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
  lv_obj_set_style_radius(st->btn_next, 3, 0);
  lv_obj_set_style_pad_all(st->btn_next, 0, 0);
  lv_obj_add_event_cb(st->btn_next, term_btn_next_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_next = lv_label_create(st->btn_next);
  lv_label_set_text(lbl_next, ">");
  lv_obj_align(lbl_next, LV_ALIGN_CENTER, 0, 0);

  st->btn_run = lv_button_create(cbar);
  lv_obj_set_size(st->btn_run, run_w, btn_h);
  lv_obj_align(st->btn_run, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(st->btn_run, lv_color_hex(TERM_CLR_RUN_BTN), 0);
  lv_obj_set_style_radius(st->btn_run, 3, 0);
  lv_obj_set_style_pad_all(st->btn_run, 0, 0);
  lv_obj_add_event_cb(st->btn_run, term_btn_run_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_run = lv_label_create(st->btn_run);
  lv_label_set_text(lbl_run, is_large ? "▶ Run" : "▶");
  lv_obj_set_style_text_color(lbl_run, lv_color_hex(EOS_THEME_TEXT_WHITE), 0);
  lv_obj_align(lbl_run, LV_ALIGN_CENTER, 0, 0);

  st->btn_kb = lv_button_create(cbar);
  lv_obj_set_size(st->btn_kb, kb_w, btn_h);
  lv_obj_align(st->btn_kb, LV_ALIGN_RIGHT_MID, -(run_w + 3), 0);
  lv_obj_set_style_bg_color(st->btn_kb, lv_color_hex(EOS_THEME_PRIMARY_BLUE), 0);
  lv_obj_set_style_radius(st->btn_kb, 3, 0);
  lv_obj_set_style_pad_all(st->btn_kb, 0, 0);
  lv_obj_add_event_cb(st->btn_kb, term_btn_kb_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_kb = lv_label_create(st->btn_kb);
  lv_label_set_text(lbl_kb, is_large ? "⌨ Edit" : "⌨");
  lv_obj_set_style_text_color(lbl_kb, lv_color_hex(EOS_THEME_TEXT_WHITE), 0);
  lv_obj_align(lbl_kb, LV_ALIGN_CENTER, 0, 0);

  int32_t cmd_box_x = (nav_w * 2) + 6;
  int32_t cmd_box_w = (int32_t)w - cmd_box_x - (kb_w + run_w + 10);
  if (cmd_box_w < TERM_MIN_CMD_BOX_W) cmd_box_w = TERM_MIN_CMD_BOX_W;

  st->cmd_box = lv_button_create(cbar);
  lv_obj_set_size(st->cmd_box, cmd_box_w, btn_h);
  lv_obj_align(st->cmd_box, LV_ALIGN_LEFT_MID, cmd_box_x, 0);
  lv_obj_set_style_bg_color(st->cmd_box, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(st->cmd_box, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(st->cmd_box, 1, 0);
  lv_obj_set_style_radius(st->cmd_box, 3, 0);
  lv_obj_set_style_pad_hor(st->cmd_box, 5, 0);
  lv_obj_set_style_pad_ver(st->cmd_box, 0, 0);
  lv_obj_add_event_cb(st->cmd_box, term_cmd_box_cb, LV_EVENT_CLICKED, st);

  st->cmd_label = lv_label_create(st->cmd_box);
  lv_obj_set_width(st->cmd_label, cmd_box_w - 10);
  lv_label_set_long_mode(st->cmd_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->cmd_label, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
  lv_obj_align(st->cmd_label, LV_ALIGN_LEFT_MID, 0, 0);
}

static void term_build_viewport_and_footer(term_viewer_state_t *st, lv_obj_t *scr,
                                           uint32_t w, uint32_t h, bool is_large,
                                           int32_t hdr_h, int32_t cbar_h,
                                           int32_t ftr_h) {
  int32_t vp_y = hdr_h + cbar_h + 2;
  int32_t vp_h = (int32_t)h - vp_y - ftr_h - 2;
  if (vp_h < TERM_MIN_VIEWPORT_H) vp_h = TERM_MIN_VIEWPORT_H;

  st->viewport = lv_obj_create(scr);
  lv_obj_set_size(st->viewport, (int32_t)w - 4, vp_h);
  lv_obj_align(st->viewport, LV_ALIGN_TOP_MID, 0, vp_y);
  lv_obj_set_style_bg_color(st->viewport, lv_color_hex(TERM_CLR_VIEWPORT_BG), 0);
  lv_obj_set_style_border_color(st->viewport, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(st->viewport, 1, 0);
  lv_obj_set_style_radius(st->viewport, 4, 0);
  lv_obj_set_style_pad_all(st->viewport, 5, 0);
  lv_obj_set_scroll_dir(st->viewport, LV_DIR_ALL);

  st->out_label = lv_label_create(st->viewport);
  lv_obj_set_width(st->out_label, (int32_t)w - 18);
  lv_label_set_long_mode(st->out_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(st->out_label, lv_color_hex(TERM_CLR_TERMINAL_GREEN), 0);
  lv_obj_align(st->out_label, LV_ALIGN_TOP_LEFT, 0, 0);

  if (is_large) {
    lv_obj_t *ftr = lv_obj_create(scr);
    lv_obj_set_size(ftr, (int32_t)w, ftr_h);
    lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ftr, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
    lv_obj_set_style_border_width(ftr, 0, 0);
    lv_obj_set_style_radius(ftr, 0, 0);
    lv_obj_set_style_pad_hor(ftr, 6, 0);
    lv_obj_set_style_pad_ver(ftr, 1, 0);

    st->footer_label = lv_label_create(ftr);
    lv_obj_set_width(st->footer_label, (int32_t)w - 12);
    lv_label_set_long_mode(st->footer_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(st->footer_label, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
    lv_obj_align(st->footer_label, LV_ALIGN_LEFT_MID, 0, 0);
  } else {
    st->footer_label = NULL;
  }
}

static void term_build_ui(term_viewer_state_t *st) {
  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  bool is_large = (h >= 400);
  int32_t hdr_h = is_large ? 32 : 22;
  int32_t cbar_h = is_large ? 38 : 24;
  int32_t ftr_h = is_large ? 26 : 0;

  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  term_build_header(st, scr, w, is_large, hdr_h);
  term_build_cbar(st, scr, w, is_large, hdr_h, cbar_h);
  term_build_viewport_and_footer(st, scr, w, h, is_large, hdr_h, cbar_h, ftr_h);

  term_update_ui(st);
}

// Shared input dialog callbacks
static void term_dialog_submit_cb(const char *text, void *user_data) {
  term_viewer_state_t *st = (term_viewer_state_t *)user_data;
  if (!st) return;
  st->is_editing = false;
  st->edit_dialog = NULL;
  if (text) {
    while (*text && isspace((unsigned char)*text)) text++;
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) len--;
    if (len > 0) {
      if (len >= sizeof(st->logic.active_cmd)) len = sizeof(st->logic.active_cmd) - 1;
      memcpy(st->logic.active_cmd, text, len);
      st->logic.active_cmd[len] = '\0';
    }
  }
  st->trigger_run = true;
  term_update_ui(st);
}

static void term_dialog_cancel_cb(const char *text, void *user_data) {
  (void)text;
  term_viewer_state_t *st = (term_viewer_state_t *)user_data;
  if (!st) return;
  st->is_editing = false;
  st->edit_dialog = NULL;
  term_update_ui(st);
}

static void term_close_edit_mode(term_viewer_state_t *st) {
  if (!st->is_editing)
    return;
  st->is_editing = false;
  if (st->edit_dialog) {
    eos_lvgl_close_input_dialog(st->edit_dialog);
    st->edit_dialog = NULL;
  }
  eos_lvgl_clear_keys();
  term_update_ui(st);
}

static void term_open_edit_mode(term_viewer_state_t *st) {
  if (st->is_editing || st->edit_dialog)
    return;

  st->is_editing = true;
  const char *initial = st->logic.active_cmd;

  eos_lvgl_input_dialog_cfg_t cfg = {
      .title = "Terminal Command",
      .prompt = "Enter command:",
      .initial_text = initial,
      .ok_label = "Run",
      .cancel_label = "Cancel",
      .is_password = false,
      .ok_cb = term_dialog_submit_cb,
      .cancel_cb = term_dialog_cancel_cb,
      .user_data = st,
  };
  st->edit_dialog = eos_lvgl_create_input_dialog(&cfg);
}

static void term_handle_key(term_viewer_state_t *st, uint16_t key) {
  if (st->is_editing) {
    if (key == KEY_ESC) {
      term_close_edit_mode(st);
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
    term_logic_prev_preset(&st->logic);
    term_update_ui(st);
    break;

  case KEY_RIGHT:
    term_logic_next_preset(&st->logic);
    term_update_ui(st);
    break;

  case KEY_A:
  case KEY_ENTER:
    st->trigger_run = true;
    break;

  case KEY_B:
  case KEY_BACKSPACE:
  case KEY_ESC:
    st->should_exit = true;
    break;

  default:
    break;
  }
}

static int term_parse_args(term_viewer_state_t *st, int argc, char **argv,
                           bool *run_cmd_arg, bool *explicit_file) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage:\n");
      printf("  <cmd> | lvgl_term\n");
      printf("  <cmd> > /term.out && lvgl_term /term.out [title] [cmd]\n");
      printf("  lvgl_term -c \"<cmd>\" [-t <timeout_ms>]\n");
      printf("  lvgl_term -t <timeout_ms>\n");
      return -1;
    } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
      snprintf(st->logic.active_cmd, sizeof(st->logic.active_cmd), "%s", argv[++i]);
      *run_cmd_arg = true;
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      st->timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (argv[i][0] != '-') {
      if (!*explicit_file) {
        snprintf(st->logic.file_path, sizeof(st->logic.file_path), "%s", argv[i]);
        snprintf(st->logic.title, sizeof(st->logic.title), "Term: %.22s", argv[i]);
        *explicit_file = true;
      } else if (!st->logic.has_exit_code && argv[i][0]) {
        snprintf(st->logic.title, sizeof(st->logic.title), "%s", argv[i]);
      }
    }
  }
  return 0;
}

static int term_cli_fallback(term_viewer_state_t *st, bool run_cmd_arg,
                             bool stdin_redirected, bool explicit_file) {
  if (run_cmd_arg) {
    char full_cmd[EOS_LARGE_STR_LEN];
    snprintf(full_cmd, sizeof(full_cmd), "%s", st->logic.active_cmd);
    system(full_cmd);
  } else if (stdin_redirected && !explicit_file) {
    char chunk[EOS_SECTOR_SIZE];
    ssize_t n;
    while ((n = read(STDIN_FILENO, chunk, sizeof(chunk))) > 0) {
      (void)write(STDOUT_FILENO, chunk, (size_t)n);
    }
  } else if (explicit_file) {
    int fd = open(st->logic.file_path, O_RDONLY);
    if (fd >= 0) {
      char chunk[EOS_SECTOR_SIZE];
      ssize_t n;
      while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
        (void)write(STDOUT_FILENO, chunk, (size_t)n);
      }
      close(fd);
    }
  }
  term_logic_deinit(&st->logic);
  free(st);
  return 0;
}

static void term_run_event_loop(term_viewer_state_t *st) {
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
      term_logic_execute_cmd(&st->logic, st->logic.active_cmd);
      if (st->viewport) {
        lv_obj_scroll_to_y(st->viewport, 0, LV_ANIM_OFF);
      }
      eos_lvgl_clear_keys();
      term_update_ui(st);
      continue;
    }

    eos_lvgl_step(10);
  }

  if (st->edit_dialog) {
    eos_lvgl_close_input_dialog(st->edit_dialog);
    st->edit_dialog = NULL;
  }
  eos_lvgl_deinit();
}

int lvgl_term_main(int argc, char **argv) {
  term_viewer_state_t *st = calloc(1, sizeof(term_viewer_state_t));
  if (!st) {
    perror("lvgl_term calloc");
    return -1;
  }

  if (term_logic_init(&st->logic) != 0) {
    free(st);
    return -1;
  }

  st->start_time_ms = get_time_ms();
  snprintf(st->logic.file_path, sizeof(st->logic.file_path), "%s", TERM_DEFAULT_OUT_FILE);

  bool run_cmd_arg = false;
  bool explicit_file = false;

  if (term_parse_args(st, argc, argv, &run_cmd_arg, &explicit_file) != 0) {
    term_logic_deinit(&st->logic);
    free(st);
    return 0;
  }

  // Sync preset index if active_cmd matches a preset
  for (int i = 0; i < TERM_PRESET_COUNT; i++) {
    if (strcmp(st->logic.active_cmd, term_logic_get_preset(i)) == 0) {
      st->logic.preset_idx = i;
      break;
    }
  }

  // Check if stdin is redirected from a pipe or file (`cmd | lvgl_term`)
  eos_proc_t *proc = eos_proc_current();
  bool stdin_redirected = (proc && eos_proc_fd_map_get(proc, STDIN_FILENO) >= 0);

  // Initialize LVGL display
  if (eos_lvgl_init(NULL) != 0) {
    return term_cli_fallback(st, run_cmd_arg, stdin_redirected, explicit_file);
  }

  eos_lvgl_clear_keys();
  term_build_ui(st);

  if (run_cmd_arg) {
    term_logic_execute_cmd(&st->logic, st->logic.active_cmd);
  } else if (stdin_redirected && !explicit_file) {
    snprintf(st->logic.title, sizeof(st->logic.title), "Piped Stream Output");
    term_logic_load_fd(&st->logic, STDIN_FILENO, TERM_DEFAULT_OUT_FILE);
  } else if (explicit_file) {
    term_logic_load_file(&st->logic, st->logic.file_path);
  } else {
    // Default initial execution of first preset
    term_logic_execute_cmd(&st->logic, st->logic.active_cmd);
  }

  term_update_ui(st);
  term_run_event_loop(st);

  term_logic_deinit(&st->logic);
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
