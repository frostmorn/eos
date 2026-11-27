#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "emisc/strlimits.h"
#include "lvgl_backlight.h"

enum {
  BL_ACT_DEC = EOS_PERCENT_MAX,
  BL_ACT_INC,
  BL_ACT_FADE,
};

typedef struct {
  backlight_device_t dev;
  lv_obj_t *menu;
  eos_menu_item_t items[EOS_BACKLIGHT_MENU_MAX];
  char labels[EOS_BACKLIGHT_MENU_MAX][EOS_SMALL_STR_LEN];
  char details[EOS_BACKLIGHT_MENU_MAX][EOS_SMALL_STR_LEN];
  bool rebuild_pending;
  bool quit;
} backlight_state_t;

static void add_bl_item(backlight_state_t *st, size_t *idx, int id,
                        const char *icon, const char *label, const char *detail) {
  if (*idx >= EOS_BACKLIGHT_MENU_MAX) return;
  strlcpy(st->labels[*idx], label ? label : "", sizeof(st->labels[*idx]));
  strlcpy(st->details[*idx], detail ? detail : "", sizeof(st->details[*idx]));
  st->items[*idx] = (eos_menu_item_t){
      .id = id,
      .icon = icon,
      .label = st->labels[*idx],
      .detail = st->details[*idx],
  };
  (*idx)++;
}

static void bl_cancel_cb(eos_menu_t *menu, void *user_data) {
  (void)menu;
  backlight_state_t *st = (backlight_state_t *)user_data;
  if (st) st->quit = true;
}

static void bl_select_cb(eos_menu_t *menu, int item_id, void *user_data) {
  (void)menu;
  backlight_state_t *st = (backlight_state_t *)user_data;
  if (!st) return;

  if (item_id == BL_ACT_DEC) {
    backlight_logic_step(&st->dev, -EOS_BACKLIGHT_DEFAULT_STEP);
  } else if (item_id == BL_ACT_INC) {
    backlight_logic_step(&st->dev, EOS_BACKLIGHT_DEFAULT_STEP);
  } else if (item_id == BL_ACT_FADE) {
    st->dev.smooth_fade = !st->dev.smooth_fade;
  } else {
    size_t pcount = 0;
    const int *presets = backlight_logic_get_presets(&pcount);
    if (item_id >= 0 && (size_t)item_id < pcount) {
      backlight_logic_apply(&st->dev, presets[item_id]);
    }
  }
  st->rebuild_pending = true;
}

static void backlight_rebuild_menu(backlight_state_t *st) {
  int prev_focus = st->menu ? eos_menu_get_focused_index(st->menu) : 0;
  if (st->menu) {
    eos_menu_close(st->menu);
    st->menu = NULL;
  }

  size_t n = 0;
  char cur_buf[EOS_SMALL_STR_LEN];
  snprintf(cur_buf, sizeof(cur_buf), "%d%%", st->dev.cur_brightness);

  add_bl_item(st, &n, BL_ACT_DEC, LV_SYMBOL_MINUS, "Brightness -", cur_buf);
  add_bl_item(st, &n, BL_ACT_INC, LV_SYMBOL_PLUS, "Brightness +", cur_buf);

  size_t pcount = 0;
  const int *presets = backlight_logic_get_presets(&pcount);
  for (size_t i = 0; i < pcount && i < EOS_BACKLIGHT_PRESET_COUNT; i++) {
    char pbuf[EOS_SMALL_STR_LEN];
    snprintf(pbuf, sizeof(pbuf), "Preset %d%%", presets[i]);
    add_bl_item(st, &n, (int)i, LV_SYMBOL_EYE_OPEN, pbuf,
                st->dev.cur_brightness == presets[i] ? "Active" : "");
  }

  add_bl_item(st, &n, BL_ACT_FADE, LV_SYMBOL_REFRESH, "Smooth Fade",
              st->dev.smooth_fade ? "ON" : "OFF");

  eos_menu_cfg_t cfg = {
      .title = "Screen Backlight",
      .icon = LV_SYMBOL_EYE_OPEN,
      .items = st->items,
      .item_count = n,
      .on_select = bl_select_cb,
      .on_cancel = bl_cancel_cb,
      .user_data = st,
  };
  st->menu = eos_open_menu(&cfg);
  if (st->menu && prev_focus >= 0 && (size_t)prev_focus < n) {
    eos_menu_focus_index(st->menu, prev_focus);
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

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);

  backlight_rebuild_menu(&st);

  while (!st.quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      if (key == LV_KEY_LEFT) {
        backlight_logic_step(&st.dev, -EOS_BACKLIGHT_DEFAULT_STEP);
        st.rebuild_pending = true;
      } else if (key == LV_KEY_RIGHT) {
        backlight_logic_step(&st.dev, EOS_BACKLIGHT_DEFAULT_STEP);
        st.rebuild_pending = true;
      } else if (st.menu) {
        eos_menu_handle_key(st.menu, key);
      }
    }
    if (st.rebuild_pending && !st.quit) {
      st.rebuild_pending = false;
      backlight_rebuild_menu(&st);
    }
    eos_lvgl_step(EOS_BACKLIGHT_STEP_MS);
  }

  if (st.menu) {
    eos_menu_close(st.menu);
    st.menu = NULL;
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
    .group       = "Settings",
    .description = "Control display backlight brightness with smooth PWM fading",
    .entry_point = lvgl_backlight_main,
};
