#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ebin/lvgl_calc/lvgl_calc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *s_calc_keys[CALC_ROWS][CALC_COLS] = {
    {"7", "8", "9", "/"},
    {"4", "5", "6", "*"},
    {"1", "2", "3", "-"},
    {"C", "0", "=", "+"},
};

typedef struct {
  int64_t acc;
  int64_t cur;
  char op;
  bool entering;
  bool error;
  int sel_r;
  int sel_c;
  bool quit;

  lv_obj_t *disp_dec;
  lv_obj_t *disp_hex;
  lv_obj_t *disp_expr;
  lv_obj_t *btns[CALC_ROWS][CALC_COLS];
} calc_state_t;

static void calc_apply_pending(calc_state_t *st) {
  if (!st->op) {
    st->acc = st->cur;
    return;
  }
  switch (st->op) {
  case '+':
    st->acc += st->cur;
    break;
  case '-':
    st->acc -= st->cur;
    break;
  case '*':
    st->acc *= st->cur;
    break;
  case '/':
    if (st->cur == 0) {
      st->error = true;
      st->acc = 0;
    } else {
      st->acc /= st->cur;
    }
    break;
  default:
    break;
  }
}

static void calc_press_token(calc_state_t *st, const char *tok) {
  if (!tok)
    return;

  char c = tok[0];
  if (c >= '0' && c <= '9') {
    if (st->error) {
      st->error = false;
      st->acc = 0;
      st->cur = 0;
      st->op = 0;
    }
    if (!st->entering) {
      st->cur = 0;
      st->entering = true;
    }
    if (st->cur < 999999999LL && st->cur > -999999999LL) {
      st->cur = st->cur * 10 + (c - '0');
    }
    return;
  }

  if (c == 'C') {
    st->acc = 0;
    st->cur = 0;
    st->op = 0;
    st->entering = false;
    st->error = false;
    return;
  }

  if (c == '=') {
    if (st->op) {
      calc_apply_pending(st);
      st->cur = st->acc;
      st->op = 0;
      st->entering = false;
    }
    return;
  }

  // Operator (+, -, *, /)
  if (st->entering) {
    calc_apply_pending(st);
    st->cur = st->acc;
    st->entering = false;
  } else if (!st->op) {
    st->acc = st->cur;
  }
  st->op = c;
}

static void calc_update_ui(calc_state_t *st) {
  int64_t val = st->entering ? st->cur : st->acc;

  if (st->error) {
    lv_label_set_text(st->disp_dec, "ERR: DIV BY 0");
    lv_label_set_text(st->disp_hex, "HEX: 0x0");
  } else {
    char dbuf[EOS_STR_48_LEN];
    snprintf(dbuf, sizeof(dbuf), "%lld", (long long)val);
    lv_label_set_text(st->disp_dec, dbuf);

    char hbuf[EOS_STR_48_LEN];
    snprintf(hbuf, sizeof(hbuf), "HEX: 0x%llX",
             (unsigned long long)(uint64_t)val);
    lv_label_set_text(st->disp_hex, hbuf);
  }

  char ebuf[EOS_STR_48_LEN];
  if (st->op) {
    snprintf(ebuf, sizeof(ebuf), "%lld %c", (long long)st->acc, st->op);
  } else {
    snprintf(ebuf, sizeof(ebuf), "Ready");
  }
  lv_label_set_text(st->disp_expr, ebuf);

  for (int r = 0; r < CALC_ROWS; r++) {
    for (int c = 0; c < CALC_COLS; c++) {
      lv_obj_t *btn = st->btns[r][c];
      if (!btn)
        continue;
      bool sel = (r == st->sel_r && c == st->sel_c);
      if (sel) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PRIMARY_BLUE), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
        lv_obj_set_style_border_width(btn, 2, 0);
      } else {
        bool is_op = (c == 3 || (r == 3 && (c == 0 || c == 2)));
        lv_obj_set_style_bg_color(
            btn, is_op ? lv_color_hex(EOS_THEME_BORDER_COLOR) : lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x475569), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
      }
    }
  }
}

static void calc_btn_click_cb(lv_event_t *e) {
  calc_state_t *st = (calc_state_t *)lv_event_get_user_data(e);
  if (!st)
    return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  for (int r = 0; r < CALC_ROWS; r++) {
    for (int c = 0; c < CALC_COLS; c++) {
      if (st->btns[r][c] == btn) {
        st->sel_r = r;
        st->sel_c = c;
        calc_press_token(st, s_calc_keys[r][c]);
        calc_update_ui(st);
        return;
      }
    }
  }
}

static void calc_back_btn_cb(lv_event_t *e) {
  calc_state_t *st = (calc_state_t *)lv_event_get_user_data(e);
  if (st) {
    st->quit = true;
  }
}

static void calc_setup_ui(calc_state_t *st, uint32_t w, uint32_t h, bool compact) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  int32_t disp_h = compact ? EOS_DEFAULT_MAGIC_42 : 68;
  lv_obj_t *disp = lv_obj_create(scr);
  lv_obj_set_size(disp, (int32_t)w - 12, disp_h);
  lv_obj_align(disp, LV_ALIGN_TOP_MID, 0, compact ? 3 : 6);
  lv_obj_set_style_bg_color(disp, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(disp, lv_color_hex(EOS_THEME_INFO_BLUE), 0);
  lv_obj_set_style_border_width(disp, 1, 0);
  lv_obj_set_style_radius(disp, 6, 0);
  lv_obj_set_style_pad_all(disp, compact ? 3 : 6, 0);
  lv_obj_clear_flag(disp, LV_OBJ_FLAG_SCROLLABLE);

  st->disp_expr = lv_label_create(disp);
  lv_obj_set_style_text_color(st->disp_expr, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
  lv_obj_align(st->disp_expr, LV_ALIGN_TOP_LEFT, 0, 0);

  st->disp_dec = lv_label_create(disp);
  lv_obj_set_style_text_color(st->disp_dec, lv_color_hex(EOS_THEME_TEXT_WHITE), 0);
  lv_obj_align(st->disp_dec, LV_ALIGN_RIGHT_MID, 0, 0);

  st->disp_hex = lv_label_create(disp);
  lv_obj_set_style_text_color(st->disp_hex, lv_color_hex(EOS_THEME_ACCENT_GREEN), 0);
  lv_obj_align(st->disp_hex, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  int32_t grid_w = (int32_t)w - 12;
  int32_t btn_w = (grid_w - (compact ? 12 : 18)) / CALC_COLS;
  int32_t btn_h = compact ? 28 : 38;
  int32_t gap = compact ? 3 : 5;
  int32_t start_y = compact ? 48 : 82;

  for (int r = 0; r < CALC_ROWS; r++) {
    for (int c = 0; c < CALC_COLS; c++) {
      lv_obj_t *btn = lv_obj_create(scr);
      lv_obj_set_size(btn, btn_w, btn_h);
      lv_obj_set_pos(btn, 6 + c * (btn_w + gap), start_y + r * (btn_h + gap));
      lv_obj_set_style_radius(btn, 5, 0);
      lv_obj_set_style_pad_all(btn, 0, 0);
      lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t *lbl = lv_label_create(btn);
      lv_label_set_text(lbl, s_calc_keys[r][c]);
      lv_obj_set_style_text_color(lbl, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
      lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_center(lbl);

      st->btns[r][c] = btn;
      lv_obj_add_event_cb(btn, calc_btn_click_cb, LV_EVENT_CLICKED, st);
    }
  }

  eos_lvgl_create_back_btn(scr, calc_back_btn_cb, st);

  if (!compact) {
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Arrows:Move A/ENT:Press C:Clear D:Del B:Exit");
    lv_obj_set_style_text_color(hint, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
}

static void calc_handle_key(calc_state_t *st, uint16_t key) {
  switch (key) {
  case KEY_UP:
    st->sel_r = (st->sel_r - 1 + CALC_ROWS) % CALC_ROWS;
    break;
  case KEY_DOWN:
    st->sel_r = (st->sel_r + 1) % CALC_ROWS;
    break;
  case KEY_LEFT:
    st->sel_c = (st->sel_c - 1 + CALC_COLS) % CALC_COLS;
    break;
  case KEY_RIGHT:
    st->sel_c = (st->sel_c + 1) % CALC_COLS;
    break;
  case KEY_A:
  case KEY_ENTER:
  case KEY_SPACE:
    calc_press_token(st, s_calc_keys[st->sel_r][st->sel_c]);
    break;
  case KEY_C:
    calc_press_token(st, "C");
    break;
  case KEY_D:
    if (st->entering) {
      st->cur /= 10;
    }
    break;
  case KEY_BACKSPACE:
  case KEY_B:
  case KEY_ESC:
    st->quit = true;
    break;
  default:
    break;
  }
  calc_update_ui(st);
}

int lvgl_calc_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_calc: failed to init LVGL\n");
    return -1;
  }

  calc_state_t st;
  memset(&st, 0, sizeof(st));

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();
  bool compact = (h <= EOS_LVGL_DIALOG_MIN_W);

  calc_setup_ui(&st, w, h, compact);

  FILE *tb = fopen("/tmp/tbar.tmp", "w");
  if (tb) {
    fprintf(tb, "@%ld Calculator\n[▲▼◄►]Keys  [A]Press  [C]Clr  [B]Exit\n", (long)getpid());
    fclose(tb);
    rename("/tmp/tbar.tmp", "/tmp/tbar");
  }

  calc_update_ui(&st);

  while (!st.quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      calc_handle_key(&st, key);
    }
    eos_lvgl_step(10);
  }

  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_calc_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_calc",
    .name = "Calculator",
    .group = "Apps",
    .description = "Keypad-driven graphical calculator with hex and decimal conversions",
    .entry_point = lvgl_calc_main,
};
