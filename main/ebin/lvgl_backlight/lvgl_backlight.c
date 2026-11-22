#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/lvgl.h"
#include "emisc/strlimits.h"
#include "lvgl_backlight.h"

typedef struct {
  backlight_device_t dev;
  bool quit;

  lv_obj_t *lbl_title;
  lv_obj_t *slider;
  lv_obj_t *lbl_val;
  lv_obj_t *btn_presets[EOS_BACKLIGHT_PRESET_COUNT];
  lv_obj_t *sw_fade;
  lv_obj_t *btn_exit;
} backlight_state_t;

static void backlight_update_ui(backlight_state_t *st) {
  if (st->slider) {
    lv_slider_set_value(st->slider, st->dev.cur_brightness, LV_ANIM_OFF);
  }
  if (st->lbl_val) {
    char buf[EOS_SMALL_STR_LEN];
    backlight_logic_format_label(st->dev.cur_brightness, buf, sizeof(buf));
    lv_label_set_text(st->lbl_val, buf);
  }
}

static void slider_event_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (!st || !st->slider) return;
  int val = lv_slider_get_value(st->slider);
  backlight_logic_apply(&st->dev, val);
  backlight_update_ui(st);
}

static void preset_event_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (!st) return;
  lv_obj_t *btn = lv_event_get_target(e);
  size_t pcount = 0;
  const int *presets = backlight_logic_get_presets(&pcount);
  for (size_t i = 0; i < pcount && i < EOS_BACKLIGHT_PRESET_COUNT; i++) {
    if (st->btn_presets[i] == btn) {
      backlight_logic_apply(&st->dev, presets[i]);
      backlight_update_ui(st);
      break;
    }
  }
}

static void fade_sw_event_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (!st || !st->sw_fade) return;
  st->dev.smooth_fade = lv_obj_has_state(st->sw_fade, LV_STATE_CHECKED);
}

static void exit_btn_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (st) st->quit = true;
}

static void backlight_setup_ui(backlight_state_t *st, uint32_t w, uint32_t h) {
  (void)h;
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);

  st->lbl_title = lv_label_create(scr);
  lv_label_set_text(st->lbl_title, "Screen Backlight");
  lv_obj_align(st->lbl_title, LV_ALIGN_TOP_MID, 0, 10);

  st->lbl_val = lv_label_create(scr);
  lv_obj_align(st->lbl_val, LV_ALIGN_TOP_MID, 0, 36);

  st->slider = lv_slider_create(scr);
  lv_slider_set_range(st->slider, 0, EOS_BACKLIGHT_MAX_DUTY);
  lv_obj_set_width(st->slider, (lv_coord_t)(w - 40));
  lv_obj_align(st->slider, LV_ALIGN_TOP_MID, 0, 68);
  lv_obj_add_event_cb(st->slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, st);

  int btn_w = (int)((w - 40) / EOS_BACKLIGHT_PRESET_COUNT) - 4;
  if (btn_w < 28) btn_w = 28;
  int start_x = -(int)((EOS_BACKLIGHT_PRESET_COUNT * (btn_w + 4)) / 2) + (btn_w / 2);

  size_t pcount = 0;
  const int *presets = backlight_logic_get_presets(&pcount);
  for (size_t i = 0; i < pcount && i < EOS_BACKLIGHT_PRESET_COUNT; i++) {
    st->btn_presets[i] = lv_btn_create(scr);
    lv_obj_set_size(st->btn_presets[i], btn_w, 28);
    lv_obj_align(st->btn_presets[i], LV_ALIGN_TOP_MID, start_x + (int)i * (btn_w + 4), 104);
    lv_obj_add_event_cb(st->btn_presets[i], preset_event_cb, LV_EVENT_CLICKED, st);

    lv_obj_t *lbl = lv_label_create(st->btn_presets[i]);
    char pbuf[EOS_TINY_STR_LEN];
    snprintf(pbuf, sizeof(pbuf), "%d%%", presets[i]);
    lv_label_set_text(lbl, pbuf);
    lv_obj_center(lbl);
  }

  lv_obj_t *lbl_fade = lv_label_create(scr);
  lv_label_set_text(lbl_fade, "Smooth Fade:");
  lv_obj_align(lbl_fade, LV_ALIGN_TOP_MID, -36, 148);

  st->sw_fade = lv_switch_create(scr);
  lv_obj_align(st->sw_fade, LV_ALIGN_TOP_MID, 40, 144);
  if (st->dev.smooth_fade) {
    lv_obj_add_state(st->sw_fade, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(st->sw_fade, fade_sw_event_cb, LV_EVENT_VALUE_CHANGED, st);

  st->btn_exit = lv_btn_create(scr);
  lv_obj_set_size(st->btn_exit, 90, 32);
  lv_obj_align(st->btn_exit, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(st->btn_exit, exit_btn_cb, LV_EVENT_CLICKED, st);

  lv_obj_t *lbl_exit = lv_label_create(st->btn_exit);
  lv_label_set_text(lbl_exit, "Done");
  lv_obj_center(lbl_exit);
}

static void backlight_handle_key(backlight_state_t *st, uint16_t key) {
  switch (key) {
  case KEY_LEFT:
  case KEY_DOWN:
    backlight_logic_step(&st->dev, -EOS_BACKLIGHT_DEFAULT_STEP);
    backlight_update_ui(st);
    break;

  case KEY_RIGHT:
  case KEY_UP:
    backlight_logic_step(&st->dev, EOS_BACKLIGHT_DEFAULT_STEP);
    backlight_update_ui(st);
    break;

  case KEY_BACKSPACE:
  case KEY_B:
  case KEY_ESC:
    st->quit = true;
    break;

  default:
    break;
  }
}

int lvgl_backlight_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_backlight: failed to initialize LVGL\n");
    return -1;
  }

  backlight_state_t st;
  memset(&st, 0, sizeof(st));

  backlight_logic_init(&st.dev);
  st.dev.smooth_fade = true;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  backlight_setup_ui(&st, w, h);
  backlight_update_ui(&st);

  FILE *tb = fopen("/tmp/tbar.tmp", "w");
  if (tb) {
    fprintf(tb, "@%ld Backlight\n[◄►]Brightness  [B]Exit\n", (long)getpid());
    fclose(tb);
    rename("/tmp/tbar.tmp", "/tmp/tbar");
  }

  while (!st.quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      backlight_handle_key(&st, key);
    }
    eos_lvgl_step(10);
  }

  backlight_logic_close(&st.dev);

  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_backlight_app = {
    EOS_BIN_INITIALIZER,
    .flags       = EOS_BIN_FLAG_GUI,
    .stack_size  = EOS_BIN_GUI_STACK_SIZE,
    .filename    = "lvgl_backlight",
    .name        = "Backlight",
    .group       = "Utilities",
    .description = "Adjust screen backlight brightness and smooth fade",
    .entry_point = lvgl_backlight_main,
};
