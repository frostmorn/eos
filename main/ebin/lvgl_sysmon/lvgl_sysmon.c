#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"
#include "lvgl_sysmon.h"

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
  return (uint64_t)tv.tv_sec * (uint64_t)EOS_MS_PER_SEC + (uint64_t)tv.tv_usec / (uint64_t)EOS_US_PER_MS;
}

static void sysmon_refresh(sysmon_ui_t *ui, uint64_t start_ms) {
  uint64_t now = get_ms();
  sysmon_snapshot_t snap;
  sysmon_logic_take_snapshot(&snap, start_ms, now);

  char ubuf[EOS_LINE_STR_LEN];
  sysmon_logic_format_uptime(snap.uptime_sec, ubuf, sizeof(ubuf));
  lv_label_set_text(ui->lbl_uptime, ubuf);

  char buf[EOS_LINE_STR_LEN];
  sysmon_logic_format_mem_row("DRAM", &snap.dram, buf, sizeof(buf));
  lv_label_set_text(ui->lbl_dram, buf);
  lv_bar_set_value(ui->bar_dram, snap.dram.usage_pct, LV_ANIM_OFF);

  sysmon_logic_format_mem_row("PSRAM", &snap.psram, buf, sizeof(buf));
  lv_label_set_text(ui->lbl_psram, buf);
  lv_bar_set_value(ui->bar_psram, snap.psram.usage_pct, LV_ANIM_OFF);

  sysmon_logic_format_mem_row("DMA", &snap.dma, buf, sizeof(buf));
  lv_label_set_text(ui->lbl_dma, buf);
  lv_bar_set_value(ui->bar_dma, snap.dma.usage_pct, LV_ANIM_OFF);

  eos_lvgl_ctx_t *ctx = eos_lvgl_get_ctx();
  if (ctx) {
    char lbuf[EOS_TITLE_STR_LEN];
    snprintf(lbuf, sizeof(lbuf), "LVGL Flushes: P:%u F:%u (%ux%u)",
             (unsigned)ctx->partial_flushes, (unsigned)ctx->full_flushes,
             (unsigned)ctx->width, (unsigned)ctx->height);
    lv_label_set_text(ui->lbl_lvgl, lbuf);
  }
}

static void sysmon_back_btn_cb(lv_event_t *e) {
  sysmon_ui_t *ui = (sysmon_ui_t *)lv_event_get_user_data(e);
  if (ui) {
    ui->quit = true;
  }
}

int lvgl_sysmon_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_sysmon: failed to init LVGL\n");
    return -1;
  }

  sysmon_ui_t ui;
  memset(&ui, 0, sizeof(ui));
  uint32_t w = eos_lvgl_get_width();

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  ui.lbl_uptime = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_uptime, lv_color_hex(EOS_THEME_INFO_BLUE), 0);
  lv_obj_align(ui.lbl_uptime, LV_ALIGN_TOP_MID, 0, 8);

  // DRAM bar
  ui.lbl_dram = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_dram, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
  lv_obj_align(ui.lbl_dram, LV_ALIGN_TOP_LEFT, 10, SYSMON_TITLE_Y);

  ui.bar_dram = lv_bar_create(scr);
  lv_obj_set_size(ui.bar_dram, (int32_t)w - SYSMON_BAR_MARGIN, SYSMON_BAR_H);
  lv_bar_set_range(ui.bar_dram, 0, 100);
  lv_obj_align(ui.bar_dram, LV_ALIGN_TOP_MID, 0, SYSMON_BAR_DRAM_Y);

  // PSRAM bar
  ui.lbl_psram = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_psram, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
  lv_obj_align(ui.lbl_psram, LV_ALIGN_TOP_LEFT, 10, SYSMON_PSRAM_LBL_Y);

  ui.bar_psram = lv_bar_create(scr);
  lv_obj_set_size(ui.bar_psram, (int32_t)w - SYSMON_BAR_MARGIN, SYSMON_BAR_H);
  lv_bar_set_range(ui.bar_psram, 0, 100);
  lv_obj_align(ui.bar_psram, LV_ALIGN_TOP_MID, 0, SYSMON_BAR_PSRAM_Y);

  // DMA bar
  ui.lbl_dma = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_dma, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
  lv_obj_align(ui.lbl_dma, LV_ALIGN_TOP_LEFT, 10, SYSMON_DMA_LBL_Y);

  ui.bar_dma = lv_bar_create(scr);
  lv_obj_set_size(ui.bar_dma, (int32_t)w - SYSMON_BAR_MARGIN, SYSMON_BAR_H);
  lv_bar_set_range(ui.bar_dma, 0, 100);
  lv_obj_align(ui.bar_dma, LV_ALIGN_TOP_MID, 0, SYSMON_BAR_DMA_Y);

  // LVGL stats box
  ui.lbl_lvgl = lv_label_create(scr);
  lv_obj_set_style_text_color(ui.lbl_lvgl, lv_color_hex(EOS_THEME_ACCENT_GREEN), 0);
  lv_obj_align(ui.lbl_lvgl, LV_ALIGN_TOP_LEFT, 10, SYSMON_STATS_Y);

  lv_obj_t *hint = lv_label_create(scr);
  lv_label_set_text(hint, "A/Tap: Refresh    B/X: Exit");
  lv_obj_set_style_text_color(hint, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

  // Standard touch exit/back button
  eos_lvgl_create_back_btn(scr, sysmon_back_btn_cb, &ui);

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
      case KEY_B:
      case KEY_BACKSPACE:
      case KEY_ESC:
        ui.quit = true;
        break;
      default:
        break;
      }
    }

    uint64_t now = get_ms();
    if (now - last_upd >= SYSMON_REFRESH_INTERVAL_MS) {
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
