#include "ecore/bin.h"
#include "ecore/lvgl.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef struct {
  lv_obj_t *lbl_uptime;
  lv_obj_t *lbl_dram;
  lv_obj_t *bar_dram;
  lv_obj_t *lbl_psram;
  lv_obj_t *bar_psram;
  lv_obj_t *lbl_dma;
  lv_obj_t *bar_dma;
  lv_obj_t *lbl_lvgl;
  bool quit;
} sysmon_ui_t;

static uint64_t get_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static void update_mem_row(uint32_t caps, const char *title, lv_obj_t *lbl,
                           lv_obj_t *bar) {
  size_t free_b = heap_caps_get_free_size(caps);
  size_t total_b = heap_caps_get_total_size(caps);
  size_t used_b = (total_b >= free_b) ? (total_b - free_b) : 0;
  int32_t pct = (total_b > 0) ? (int32_t)((used_b * 100ULL) / total_b) : 0;

  char buf[64];
  snprintf(buf, sizeof(buf), "%s: %u / %u KB (%d%%)", title,
           (unsigned)(used_b / 1024U), (unsigned)(total_b / 1024U), (int)pct);
  lv_label_set_text(lbl, buf);
  lv_bar_set_value(bar, pct, LV_ANIM_OFF);
}

static void sysmon_refresh(sysmon_ui_t *ui, uint64_t start_ms) {
  uint64_t elapsed_s = (get_ms() - start_ms) / 1000ULL;
  eos_lvgl_ctx_t *ctx = eos_lvgl_get_ctx();

  char ubuf[64];
  snprintf(ubuf, sizeof(ubuf), "EOS System Monitor   Up: %llus",
           (unsigned long long)elapsed_s);
  lv_label_set_text(ui->lbl_uptime, ubuf);

  update_mem_row(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, "DRAM", ui->lbl_dram,
                 ui->bar_dram);
  update_mem_row(MALLOC_CAP_SPIRAM, "PSRAM", ui->lbl_psram, ui->bar_psram);
  update_mem_row(MALLOC_CAP_DMA, "DMA", ui->lbl_dma, ui->bar_dma);

  if (ctx) {
    char lbuf[80];
    snprintf(lbuf, sizeof(lbuf), "LVGL [%s] P:%u F:%u (%ux%u)",
             ctx->fallback_full ? "FULL" : "PARTIAL",
             (unsigned)ctx->partial_flushes, (unsigned)ctx->full_flushes,
             (unsigned)ctx->width, (unsigned)ctx->height);
    lv_label_set_text(ui->lbl_lvgl, lbuf);
  }
}

int lvgl_sysmon_main(int argc, char **argv) {
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
    printf("lvgl_sysmon: failed to init LVGL\n");
    return -1;
  }

  sysmon_ui_t ui;
  memset(&ui, 0, sizeof(ui));
  uint32_t w = eos_lvgl_get_width();

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1120), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  ui.lbl_uptime = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_uptime, lv_color_hex(0x38BDF8), 0);
  lv_obj_align(ui.lbl_uptime, LV_ALIGN_TOP_MID, 0, 8);

  // DRAM bar
  ui.lbl_dram = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_dram, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(ui.lbl_dram, LV_ALIGN_TOP_LEFT, 10, 36);

  ui.bar_dram = lv_bar_create(scr);
  lv_obj_set_size(ui.bar_dram, (int32_t)w - 20, 14);
  lv_bar_set_range(ui.bar_dram, 0, 100);
  lv_obj_align(ui.bar_dram, LV_ALIGN_TOP_MID, 0, 56);

  // PSRAM bar
  ui.lbl_psram = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_psram, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(ui.lbl_psram, LV_ALIGN_TOP_LEFT, 10, 84);

  ui.bar_psram = lv_bar_create(scr);
  lv_obj_set_size(ui.bar_psram, (int32_t)w - 20, 14);
  lv_bar_set_range(ui.bar_psram, 0, 100);
  lv_obj_align(ui.bar_psram, LV_ALIGN_TOP_MID, 0, 104);

  // DMA bar
  ui.lbl_dma = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_dma, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(ui.lbl_dma, LV_ALIGN_TOP_LEFT, 10, 132);

  ui.bar_dma = lv_bar_create(scr);
  lv_obj_set_size(ui.bar_dma, (int32_t)w - 20, 14);
  lv_bar_set_range(ui.bar_dma, 0, 100);
  lv_obj_align(ui.bar_dma, LV_ALIGN_TOP_MID, 0, 152);

  // LVGL stats box
  ui.lbl_lvgl = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_lvgl, lv_color_hex(0x34D399), 0);
  lv_obj_align(ui.lbl_lvgl, LV_ALIGN_TOP_LEFT, 10, 184);

  lv_obj_t *hint = lv_label_create(scr);
  lv_label_set_text(hint, "A:Refresh  C:Partial/Full  D:FullFlush  B/ESC:Exit");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

  uint64_t start_ms = get_ms();
  uint64_t last_upd = 0;
  sysmon_refresh(&ui, start_ms);

  while (!ui.quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      switch (key) {
      case KEY_A:
      case KEY_ENTER:
        sysmon_refresh(&ui, start_ms);
        break;
      case KEY_C:
        eos_lvgl_set_full_fallback(!eos_lvgl_is_full_fallback());
        sysmon_refresh(&ui, start_ms);
        break;
      case KEY_D:
        eos_lvgl_flush_full_frame();
        sysmon_refresh(&ui, start_ms);
        break;
      case KEY_B:
      case KEY_ESC:
        ui.quit = true;
        break;
      default:
        break;
      }
    }

    uint64_t now = get_ms();
    if (now - last_upd >= 250ULL) {
      last_upd = now;
      sysmon_refresh(&ui, start_ms);
    }

    eos_lvgl_step(10);
  }

  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_sysmon_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_sysmon",
    .name = "System Monitor",
    .group = "Tools",
    .description = "Real-time graphical monitor for CPU, DRAM, PSRAM, and display framerate",
    .entry_point = lvgl_sysmon_main,
};
