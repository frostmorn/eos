#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ebin/bootlogo/bootlogo.h"

#include <stdio.h>
#include <stdlib.h>

static void bootlogo_style_label(lv_obj_t *lbl, int32_t box_w, uint32_t hex,
                                 int32_t y_offset) {
  lv_obj_set_width(lbl, box_w);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(hex), 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, y_offset);
}

static void bootlogo_render(uint32_t width, uint32_t height) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(BOOTLOGO_BG_HEX), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  int32_t box_w = (int32_t)width - (BOOTLOGO_PAD_X * 2);
  if (box_w < 1) {
    box_w = (int32_t)width;
  }

  lv_obj_t *lbl_logo = lv_label_create(scr);
  lv_label_set_text(lbl_logo, BOOTLOGO_ASCII);
  bootlogo_style_label(lbl_logo, box_w, BOOTLOGO_LOGO_HEX, BOOTLOGO_LOGO_Y_OFFSET);
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(lbl_logo, &lv_font_montserrat_28, 0);
#endif

  lv_obj_t *lbl_title = lv_label_create(scr);
  lv_label_set_text(lbl_title, BOOTLOGO_TITLE);
  bootlogo_style_label(lbl_title, box_w, BOOTLOGO_TEXT_HEX, BOOTLOGO_TITLE_Y_OFFSET);

  char info_buf[EOS_LINE_STR_LEN];
  snprintf(info_buf, sizeof(info_buf), "IDF %s | %ux%u",
           IDF_VER, (unsigned)width, (unsigned)height);
  lv_obj_t *lbl_info = lv_label_create(scr);
  lv_label_set_text(lbl_info, info_buf);
  bootlogo_style_label(lbl_info, box_w, BOOTLOGO_MUTED_HEX, BOOTLOGO_INFO_Y_OFFSET);
}

int bootlogo_main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  const char *fb_path = getenv("EOS_FB");
  if (eos_lvgl_init_paths(fb_path, NULL) != 0) {
    return -1;
  }

  uint32_t width = eos_lvgl_get_width();
  uint32_t height = eos_lvgl_get_height();
  bootlogo_render(width, height);

  eos_lvgl_step(BOOTLOGO_STEP_MS);
  eos_lvgl_step(BOOTLOGO_STEP_MS);
  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t bootlogo_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_HIDDEN,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "bootlogo",
    .name = "Boot Logo",
    .group = "System",
    .description = "Render EOS ASCII boot splash logo and version, then exit immediately",
    .entry_point = bootlogo_main,
};
