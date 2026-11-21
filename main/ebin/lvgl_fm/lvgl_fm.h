#pragma once

#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

#define FM_MAX_ENTRIES        256
#define FM_HDR_HEIGHT_DEFAULT 38
#define FM_HDR_HEIGHT_COMPACT 32
#define FM_BAR_HEIGHT         30
#define FM_VIEW_MAX_BYTES     2048
#define FM_BYTES_PER_KB       EOS_LARGE_STR_LEN
#define FM_BYTES_PER_MB       (EOS_LARGE_STR_LEN * EOS_LARGE_STR_LEN)
#define FM_ACTIONS_MODAL_W    210
#define FM_ACTIONS_BTN_H      30
#define FM_VIEW_MODAL_W_PCT   90
#define FM_VIEW_MODAL_H_PCT   85
#define FM_VIEW_CLOSE_BTN_W_PCT 45
#define FM_BTN_WIDTH_PCT      100
#define FM_BTN_SHORTCUT_W     54
#define FM_BTN_SHORTCUT_H     24
#define FM_BTN_ICON_W         32
#define FM_BTN_ICON_H         26

typedef struct {
  char name[EOS_SMALL_STR_LEN];
  bool is_dir;
  bool is_exec;
  size_t size;
} fm_entry_t;

typedef struct fm_state_s {
  char cwd[EOS_MID_STR_LEN];
  char pending_nav_path[EOS_MID_STR_LEN];
  fm_entry_t *entries;
  int entry_count;
  int selected_idx;
  bool quit;
  bool auto_mode;
  int timeout_ms;

  lv_obj_t *scr;
  lv_obj_t *hdr;
  lv_obj_t *path_lbl;
  lv_obj_t *list;
  lv_obj_t *dialog;
  lv_obj_t **list_btns;
  int list_btns_count;

  // Selected item cache for action dialogs
  char selected_name[EOS_SMALL_STR_LEN];
  char selected_path[EOS_MID_STR_LEN];
  bool selected_is_dir;
} fm_state_t;

int lvgl_fm_main(int argc, char **argv);
