#include "ecore/bin.h"
#include "ecore/lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
  int32_t box_x;
  int32_t box_y;
  int32_t progress;
  uint32_t color_idx;
  bool running;
  lv_obj_t *mode_label;
  lv_obj_t *stats_label;
  lv_obj_t *key_label;
  lv_obj_t *bar;
  lv_obj_t *sprite;
} test_lvgl_ui_t;

static const uint32_t s_test_colors[] = {
    0x38BDF8, 0x34D399, 0xFBBF24, 0xF87171, 0xA78BFA, 0xF472B6,
};

static void update_test_ui(test_lvgl_ui_t *ui, const char *last_key_name) {
  eos_lvgl_ctx_t *ctx = eos_lvgl_get_ctx();
  if (!ctx)
    return;

  char mode_buf[64];
  snprintf(mode_buf, sizeof(mode_buf), "Mode: %s (%ux%u, strip=%u)",
           ctx->fallback_full ? "FULL FALLBACK" : "PARTIAL DRAW",
           (unsigned)ctx->width, (unsigned)ctx->height,
           (unsigned)ctx->partial_lines);
  lv_label_set_text(ui->mode_label, mode_buf);

  char stats_buf[64];
  snprintf(stats_buf, sizeof(stats_buf), "Flushes: partial=%u  full=%u",
           (unsigned)ctx->partial_flushes, (unsigned)ctx->full_flushes);
  lv_label_set_text(ui->stats_label, stats_buf);

  if (last_key_name) {
    char kbuf[64];
    snprintf(kbuf, sizeof(kbuf), "Last key: %s | Pos:(%d,%d)", last_key_name,
             (int)ui->box_x, (int)ui->box_y);
    lv_label_set_text(ui->key_label, kbuf);
  }

  lv_bar_set_value(ui->bar, ui->progress, LV_ANIM_OFF);
  lv_obj_set_pos(ui->sprite, ui->box_x, ui->box_y);
  lv_obj_set_style_bg_color(
      ui->sprite,
      lv_color_hex(s_test_colors[ui->color_idx %
                                 (sizeof(s_test_colors) / sizeof(s_test_colors[0]))]),
      0);
}

static void scr_touch_event_cb(lv_event_t *e) {
  test_lvgl_ui_t *ui = (test_lvgl_ui_t *)lv_event_get_user_data(e);
  lv_indev_t *indev = lv_indev_active();
  if (indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    ui->box_x = p.x - 18;
    ui->box_y = p.y - 18;
    update_test_ui(ui, "TOUCH");
  }
}

static void test_lvgl_back_btn_cb(lv_event_t *e) {
  test_lvgl_ui_t *ui = (test_lvgl_ui_t *)lv_event_get_user_data(e);
  if (ui) {
    ui->running = false;
  }
}

static int test_lvgl_main(int argc, char **argv) {
  const char *fb_path = NULL;
  const char *kbd_path = NULL;
  bool auto_mode = false;
  int timeout_ms = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto") == 0) {
      auto_mode = true;
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      timeout_ms = atoi(argv[++i]);
    } else if (argv[i][0] == '/') {
      if (!fb_path) fb_path = argv[i];
      else if (!kbd_path) kbd_path = argv[i];
    }
  }

  printf("Starting LVGL Framebuffer Test (partial draw + full fallback)...\n");

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("Failed to initialize EOS LVGL framebuffer support\n");
    return -1;
  }

  uint32_t width = eos_lvgl_get_width();
  uint32_t height = eos_lvgl_get_height();
  printf("Display resolution: %ux%u\n", (unsigned)width, (unsigned)height);

  test_lvgl_ui_t ui;
  memset(&ui, 0, sizeof(ui));
  ui.running = true;
  ui.progress = 50;
  ui.box_x = ((int32_t)width - 36) / 2;
  ui.box_y = 110;

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "EOS + LVGL FB Test");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  ui.mode_label = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.mode_label, lv_color_hex(0x34D399), 0);
  lv_obj_align(ui.mode_label, LV_ALIGN_TOP_MID, 0, 26);

  ui.stats_label = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.stats_label, lv_color_hex(0x93C5FD), 0);
  lv_obj_align(ui.stats_label, LV_ALIGN_TOP_MID, 0, 44);

  ui.key_label = lv_label_create(scr);
  lv_label_set_text(ui.key_label, "Keys: Arrows=Move A=Color C=Mode B=Exit");
  lv_obj_set_style_text_color(ui.key_label, lv_color_hex(0xFDE047), 0);
  lv_obj_align(ui.key_label, LV_ALIGN_TOP_MID, 0, 62);

  ui.bar = lv_bar_create(scr);
  lv_obj_set_size(ui.bar, (int32_t)width - 24, 14);
  lv_bar_set_range(ui.bar, 0, 100);
  lv_obj_align(ui.bar, LV_ALIGN_TOP_MID, 0, 84);

  ui.sprite = lv_obj_create(scr);
  lv_obj_set_size(ui.sprite, 36, 36);
  lv_obj_set_style_radius(ui.sprite, 6, 0);
  lv_obj_set_style_border_color(ui.sprite, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(ui.sprite, 2, 0);

  lv_obj_t *hint = lv_label_create(scr);
  lv_label_set_text(hint, "C: Partial/Full  D: FullRedraw  B/ESC: Quit");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

  lv_obj_add_event_cb(scr, scr_touch_event_cb, LV_EVENT_PRESSED, &ui);
  lv_obj_add_event_cb(scr, scr_touch_event_cb, LV_EVENT_PRESSING, &ui);

  // Standard touch exit/back button
  eos_lvgl_create_back_btn(scr, test_lvgl_back_btn_cb, &ui);

  update_test_ui(&ui, NULL);

  if (auto_mode) {
    printf("Running automated LVGL partial and full fallback test...\n");
    // 1. Partial mode test
    eos_lvgl_set_full_fallback(false);
    ui.progress = 25;
    update_test_ui(&ui, "AUTO_PARTIAL");
    eos_lvgl_step(50);
    uint32_t p_flushes1 = eos_lvgl_get_ctx() ? eos_lvgl_get_ctx()->partial_flushes : 0;

    // 2. Full-frame fallback test
    eos_lvgl_set_full_fallback(true);
    ui.progress = 75;
    update_test_ui(&ui, "AUTO_FULL");
    eos_lvgl_step(50);
    uint32_t f_flushes = eos_lvgl_get_ctx() ? eos_lvgl_get_ctx()->full_flushes : 0;

    // 3. Switch back to partial mode
    eos_lvgl_set_full_fallback(false);
    ui.progress = 100;
    update_test_ui(&ui, "AUTO_DONE");
    eos_lvgl_step(50);
    uint32_t p_flushes2 = eos_lvgl_get_ctx() ? eos_lvgl_get_ctx()->partial_flushes : 0;

    printf("Automated test flushes: partial=%u->%u, full=%u\n",
           (unsigned)p_flushes1, (unsigned)p_flushes2, (unsigned)f_flushes);
    printf("LVGL Framebuffer Test PASSED\n");
    ui.running = false;
  }

  uint64_t start_ms = 0;
  if (timeout_ms > 0) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    start_ms = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
  }

  while (ui.running) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      switch (key) {
      case KEY_LEFT:
        ui.box_x -= 10;
        if (ui.box_x < 4)
          ui.box_x = 4;
        if (ui.progress >= 5)
          ui.progress -= 5;
        break;
      case KEY_RIGHT:
        ui.box_x += 10;
        if (ui.box_x > (int32_t)width - 40)
          ui.box_x = (int32_t)width - 40;
        if (ui.progress <= 95)
          ui.progress += 5;
        break;
      case KEY_UP:
        ui.box_y -= 10;
        if (ui.box_y < 104)
          ui.box_y = 104;
        break;
      case KEY_DOWN:
        ui.box_y += 10;
        if (ui.box_y > (int32_t)height - 60)
          ui.box_y = (int32_t)height - 60;
        break;
      case KEY_A:
      case KEY_ENTER:
      case KEY_SPACE:
        ui.color_idx++;
        break;
      case KEY_C:
        eos_lvgl_set_full_fallback(!eos_lvgl_is_full_fallback());
        break;
      case KEY_D:
        eos_lvgl_flush_full_frame();
        break;
      case KEY_B:
      case KEY_BACKSPACE:
      case KEY_ESC:
        ui.running = false;
        break;
      default:
        break;
      }
      update_test_ui(&ui, eos_lvgl_key_name(key));
    }

    eos_lvgl_step(10);
    update_test_ui(&ui, NULL);

    if (timeout_ms > 0) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      uint64_t now = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
      if (now - start_ms >= (uint64_t)timeout_ms) {
        ui.running = false;
      }
    }
  }

  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_test_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "test_lvgl",
    .name = "LVGL Test",
    .group = "Tests",
    .description = "Test LVGL display rendering pipelines and keypad input handling",
    .entry_point = test_lvgl_main,
};
