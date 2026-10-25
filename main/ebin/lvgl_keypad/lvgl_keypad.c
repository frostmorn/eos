#include "ecore/bin.h"
#include "ecore/lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint16_t code;
  const char *label;
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} key_cap_def_t;

// Action buttons layout on Lilka v2.3:
// C = top, B = bottom, D = left, A = right
static const key_cap_def_t s_key_caps[] = {
    {KEY_UP, "UP", 40, 32, 36, 26},
    {KEY_LEFT, "L", 6, 62, 32, 26},
    {KEY_RIGHT, "R", 78, 62, 32, 26},
    {KEY_DOWN, "DN", 40, 92, 36, 26},
    {KEY_ENTER, "SEL", 92, 124, 44, 22},
    {KEY_SPACE, "STA", 144, 124, 44, 22},
    {KEY_C, "C", 188, 32, 36, 26},
    {KEY_D, "D", 150, 62, 36, 26},
    {KEY_A, "A", 226, 62, 36, 26},
    {KEY_B, "B", 188, 92, 36, 26},
};

#define KEY_CAP_COUNT (sizeof(s_key_caps) / sizeof(s_key_caps[0]))

int lvgl_keypad_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  eos_lvgl_cfg_t cfg = {
      .fb_path = fb_path,
      .kbd_path = kbd_path,
      .partial_lines = 0,
      .force_full_frame = false,
      .dpad_group_nav = false,
      .enable_keyboard = true,
  };

  if (eos_lvgl_init(&cfg) != 0) {
    printf("lvgl_keypad: failed to init LVGL\n");
    return -1;
  }

  uint32_t w = eos_lvgl_get_width();

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1120), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *hdr = lv_label_create(scr);
  lv_label_set_text(hdr, "LVGL Keypad Tester (ESC or 3x B to exit)");
  lv_obj_set_style_text_color(hdr, lv_color_hex(0x38BDF8), 0);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 6);

  lv_obj_t *cap_objs[KEY_CAP_COUNT] = {0};
  for (size_t i = 0; i < KEY_CAP_COUNT; i++) {
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, s_key_caps[i].w, s_key_caps[i].h);
    lv_obj_set_pos(box, s_key_caps[i].x, s_key_caps[i].y);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 5, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, s_key_caps[i].label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xF8FAFC), 0);
    lv_obj_center(lbl);
    cap_objs[i] = box;
  }

  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_size(panel, (int32_t)w - 12, 82);
  lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 6, 0);
  lv_obj_set_style_pad_all(panel, 6, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_last = lv_label_create(panel);
  lv_label_set_text(lbl_last, "Press any button...");
  lv_obj_set_style_text_color(lbl_last, lv_color_hex(0xFDE047), 0);
  lv_obj_align(lbl_last, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *lbl_lv = lv_label_create(panel);
  lv_label_set_text(lbl_lv, "LVGL key: -   Count: 0");
  lv_obj_set_style_text_color(lbl_lv, lv_color_hex(0xE2E8F0), 0);
  lv_obj_align(lbl_lv, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *lbl_fb = lv_label_create(panel);
  lv_label_set_text(lbl_fb, "FB mode: PARTIAL");
  lv_obj_set_style_text_color(lbl_fb, lv_color_hex(0x34D399), 0);
  lv_obj_align(lbl_fb, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  uint32_t total_events = 0;
  int consecutive_b = 0;
  bool running = true;

  while (running) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      total_events++;

      if (key == KEY_ESC) {
        running = false;
        break;
      }
      if (key == KEY_B) {
        consecutive_b++;
        if (consecutive_b >= 3) {
          running = false;
          break;
        }
      } else {
        consecutive_b = 0;
      }

      for (size_t i = 0; i < KEY_CAP_COUNT; i++) {
        if (s_key_caps[i].code == key) {
          lv_obj_set_style_bg_color(cap_objs[i], lv_color_hex(0x2563EB), 0);
          lv_obj_set_style_border_color(cap_objs[i], lv_color_hex(0x93C5FD), 0);
        } else {
          lv_obj_set_style_bg_color(cap_objs[i], lv_color_hex(0x1E293B), 0);
          lv_obj_set_style_border_color(cap_objs[i], lv_color_hex(0x475569), 0);
        }
      }

      char buf1[64];
      snprintf(buf1, sizeof(buf1), "Key: %s (code=%u)", eos_lvgl_key_name(key),
               (unsigned)key);
      lv_label_set_text(lbl_last, buf1);

      uint32_t lv_k = eos_lvgl_map_key(key, false);
      char buf2[64];
      snprintf(buf2, sizeof(buf2), "LVGL key: %u  Events: %u (B:%d/3)",
               (unsigned)lv_k, (unsigned)total_events, consecutive_b);
      lv_label_set_text(lbl_lv, buf2);
    }

    eos_lvgl_ctx_t *ctx = eos_lvgl_get_ctx();
    if (ctx) {
      char buf3[64];
      snprintf(buf3, sizeof(buf3), "FB: %s (P:%u F:%u)",
               ctx->fallback_full ? "FULL" : "PARTIAL",
               (unsigned)ctx->partial_flushes, (unsigned)ctx->full_flushes);
      lv_label_set_text(lbl_fb, buf3);
    }

    eos_lvgl_step(10);
  }

  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_keypad_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_keypad",
    .name = "Keypad Tester",
    .group = "Tests",
    .description = "Interactive test utility visualizing keypad scan matrix and key events",
    .entry_point = lvgl_keypad_main,
};
