#include "lvgl_fm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static void fm_render_file_list(fm_state_t *st);
static void fm_setup_ui(fm_state_t *st, uint32_t w, uint32_t h);

static uint64_t fm_get_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((uint64_t)tv.tv_sec * (uint64_t)EOS_MS_PER_SEC) +
         ((uint64_t)tv.tv_usec / (uint64_t)EOS_US_PER_MS);
}

static void fm_close_action_menu(fm_state_t *st) {
  if (st && st->action_menu) {
    eos_menu_close(st->action_menu);
    st->action_menu = NULL;
    eos_lvgl_clear_keys();
  }
}

static void fm_close_dialog(fm_state_t *st) {
  if (st && st->dialog) {
    eos_lvgl_dialog_close(st->dialog);
    st->dialog = NULL;
    eos_lvgl_clear_keys();
  }
}

static void fm_reset_widget_ptrs(fm_state_t *st) {
  if (!st) return;
  st->scr = NULL;
  st->hdr = NULL;
  st->path_lbl = NULL;
  st->quick_bar = NULL;
  st->menu_obj = NULL;
  st->action_menu = NULL;
  st->dialog = NULL;
}

static void fm_preview_dialog_cb(void *user_data) {
  fm_state_t *st = (fm_state_t *)user_data;
  if (st) st->dialog = NULL;
}

static void fm_show_file_preview(fm_state_t *st) {
  fm_close_dialog(st);

  int fd = open(st->selected_path, O_RDONLY);
  char *view_buf = calloc(1, FM_VIEW_MAX_BYTES + 1);
  if (fd >= 0) {
    if (view_buf) {
      ssize_t n = read(fd, view_buf, FM_VIEW_MAX_BYTES);
      if (n >= 0) view_buf[n] = '\0';
    }
    close(fd);
  }

  eos_lvgl_preview_dialog_cfg_t cfg = {
    .title = st->selected_name,
    .content = view_buf ? view_buf : "Error reading file",
    .close_label = "Close",
    .cb = fm_preview_dialog_cb,
    .user_data = st,
  };
  st->dialog = eos_lvgl_dialog_preview(&cfg);
  free(view_buf);
}

static void fm_delete_confirmed_cb(bool confirmed, void *user_data) {
  fm_state_t *st = (fm_state_t *)user_data;
  if (!st) return;
  st->dialog = NULL;
  if (!confirmed) return;

  int rc = -1;
  if (st->selected_is_dir) {
    rc = rmdir(st->selected_path);
  } else {
    rc = unlink(st->selected_path);
  }
  if (rc == 0) {
    (void)eos_toolbar_printf(NULL, "Deleted %s", st->selected_name);
  } else {
    (void)eos_toolbar_printf(NULL, "Failed to delete: %s", strerror(errno));
  }
  strlcpy(st->pending_nav_path, st->data.cwd, sizeof(st->pending_nav_path));
}

static void fm_trigger_delete_confirm(fm_state_t *st) {
  if (!st) return;
  char msg[EOS_MID_STR_LEN];
  snprintf(msg, sizeof(msg), "Delete %s?", st->selected_name);
  eos_lvgl_confirm_dialog_cfg_t cfg = {
    .title = "Confirm Delete",
    .message = msg,
    .confirm_label = "Delete",
    .cancel_label = "Cancel",
    .dangerous = true,
    .cb = fm_delete_confirmed_cb,
    .user_data = st,
  };
  st->dialog = eos_lvgl_dialog_confirm(&cfg);
}

static void fm_action_menu_cb(eos_menu_t *menu, int item_id, void *user_data) {
  (void)menu;
  fm_state_t *st = (fm_state_t *)user_data;
  if (!st) return;

  fm_close_action_menu(st);

  switch (item_id) {
  case 0: // Open
    if (st->selected_is_dir) {
      strlcpy(st->pending_nav_path, st->selected_path, sizeof(st->pending_nav_path));
      eos_lvgl_clear_keys();
    } else {
      (void)eos_toolbar_printf(NULL, "Opening %s", st->selected_name);
      if (st->selected_is_exec) {
        char launch_cmd[EOS_MID_STR_LEN];
        snprintf(launch_cmd, sizeof(launch_cmd), "%s", st->selected_path);
        fm_close_dialog(st);
        eos_lvgl_deinit();
        fm_reset_widget_ptrs(st);
        if (st->details) {
          free(st->details);
          st->details = NULL;
        }
        system(launch_cmd);
        eos_lvgl_init_paths(NULL, NULL);
        uint32_t w = eos_lvgl_get_width();
        uint32_t h = eos_lvgl_get_height();
        fm_setup_ui(st, w, h);
        fm_render_file_list(st);
      } else {
        fm_show_file_preview(st);
      }
    }
    break;

  case 1: // Preview
    fm_show_file_preview(st);
    break;

  case 2: // Delete
    fm_trigger_delete_confirm(st);
    break;

  case 3: // Cancel
  default:
    break;
  }
}

static void fm_show_file_actions(fm_state_t *st, size_t entry_idx) {
  if (entry_idx >= kv_size(st->data.entries)) return;
  fm_close_action_menu(st);
  fm_close_dialog(st);
  eos_lvgl_clear_keys();

  const fm_entry_t *entry = &kv_A(st->data.entries, entry_idx);
  strlcpy(st->selected_name, entry->name, sizeof(st->selected_name));
  st->selected_is_dir = fm_logic_is_dir(entry);
  st->selected_is_exec = fm_logic_is_exec(entry);
  fm_logic_get_full_path(&st->data, entry_idx, st->selected_path, sizeof(st->selected_path));

  static const eos_menu_item_t action_items[] = {
    { .id = 0, .icon = LV_SYMBOL_PLAY,     .label = "Open",    .detail = NULL, .is_destructive = false, .is_disabled = false },
    { .id = 1, .icon = LV_SYMBOL_EYE_OPEN, .label = "Preview", .detail = NULL, .is_destructive = false, .is_disabled = false },
    { .id = 2, .icon = LV_SYMBOL_TRASH,    .label = "Delete",  .detail = NULL, .is_destructive = true,  .is_disabled = false },
    { .id = 3, .icon = LV_SYMBOL_CLOSE,    .label = "Cancel",  .detail = NULL, .is_destructive = false, .is_disabled = false },
  };

  eos_menu_cfg_t cfg = {
    .title = st->selected_name,
    .icon = fm_logic_get_icon(entry),
    .items = action_items,
    .item_count = sizeof(action_items) / sizeof(action_items[0]),
    .on_select = fm_action_menu_cb,
    .user_data = st,
    .width = 0,
    .parent = st->scr,
  };

  st->action_menu = eos_open_menu(&cfg);
}

static void fm_entry_selected_cb(eos_menu_t *menu, int item_id, void *user_data) {
  (void)menu;
  fm_state_t *st = (fm_state_t *)user_data;
  if (!st || item_id < 0 || (size_t)item_id >= kv_size(st->data.entries)) return;

  const fm_entry_t *e = &kv_A(st->data.entries, (size_t)item_id);

  if (strcmp(e->name, "..") == 0) {
    char parent[EOS_MID_STR_LEN];
    fm_logic_parent_path(st->data.cwd, parent, sizeof(parent));
    strlcpy(st->pending_nav_path, parent, sizeof(st->pending_nav_path));
    eos_lvgl_clear_keys();
    return;
  }

  if (fm_logic_is_dir(e)) {
    char next[EOS_MID_STR_LEN];
    fm_logic_get_full_path(&st->data, (size_t)item_id, next, sizeof(next));
    strlcpy(st->pending_nav_path, next, sizeof(st->pending_nav_path));
    eos_lvgl_clear_keys();
  } else {
    fm_show_file_actions(st, (size_t)item_id);
  }
}

static void fm_mkdir_confirm_cb(const char *name, void *user_data) {
  fm_state_t *st = (fm_state_t *)user_data;
  if (!st || !name || name[0] == '\0') return;

  if (fm_logic_create_folder(&st->data, name) == 0) {
    (void)eos_toolbar_printf(NULL, "Created %s", name);
    strlcpy(st->pending_nav_path, st->data.cwd, sizeof(st->pending_nav_path));
  } else {
    (void)eos_toolbar_printf(NULL, "mkdir failed: %s", strerror(errno));
  }
}

static void fm_trigger_mkdir(fm_state_t *st) {
  if (!st) return;
  eos_lvgl_input_dialog_cfg_t cfg = {
    .title = "New Folder",
    .prompt = "Folder Name:",
    .initial_text = "new_folder",
    .ok_cb = fm_mkdir_confirm_cb,
    .user_data = st,
  };
  (void)eos_lvgl_create_input_dialog(&cfg);
}

static void fm_mkdir_btn_cb(lv_event_t *e) {
  fm_state_t *st = (fm_state_t *)lv_event_get_user_data(e);
  fm_trigger_mkdir(st);
}

static void fm_refresh_btn_cb(lv_event_t *e) {
  fm_state_t *st = (fm_state_t *)lv_event_get_user_data(e);
  if (!st) return;
  strlcpy(st->pending_nav_path, st->data.cwd, sizeof(st->pending_nav_path));
}

static void fm_navigate_up(fm_state_t *st) {
  if (!st) return;
  if (strcmp(st->data.cwd, "/") == 0) {
    st->quit = true;
    return;
  }
  char parent[EOS_MID_STR_LEN];
  fm_logic_parent_path(st->data.cwd, parent, sizeof(parent));
  strlcpy(st->pending_nav_path, parent, sizeof(st->pending_nav_path));
  eos_lvgl_clear_keys();
}

static void fm_back_btn_cb(lv_event_t *e) {
  fm_state_t *st = (fm_state_t *)lv_event_get_user_data(e);
  fm_navigate_up(st);
}

static void fm_jump_btn_cb(lv_event_t *e) {
  fm_state_t *st = (fm_state_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  const char *target = (const char *)lv_obj_get_user_data(btn);
  if (st && target) {
    strlcpy(st->pending_nav_path, target, sizeof(st->pending_nav_path));
  }
}

static void fm_entry_focus_cb(eos_menu_t *menu, int item_id, void *user_data) {
  (void)menu;
  fm_state_t *st = (fm_state_t *)user_data;
  if (!st || item_id < 0 || (size_t)item_id >= kv_size(st->data.entries)) return;
  st->focused_idx = item_id;
}

static void fm_render_file_list(fm_state_t *st) {
  if (!st) return;

  if (st->path_lbl) {
    lv_label_set_text(st->path_lbl, st->data.cwd);
  }

  if (st->menu_obj) {
    lv_obj_del(st->menu_obj);
    st->menu_obj = NULL;
  }
  if (st->details) {
    free(st->details);
    st->details = NULL;
  }

  uint32_t sw = eos_lvgl_get_width();
  uint32_t sh = eos_lvgl_get_height();
  bool is_small = eos_screen_is_compact();

  size_t count = kv_size(st->data.entries);
  eos_menu_item_t *items = NULL;

  if (count > 0) {
    items = calloc(count, sizeof(eos_menu_item_t));
    st->details = calloc(count, sizeof(*st->details));
  }

  if (items && st->details) {
    for (size_t i = 0; i < count; i++) {
      const fm_entry_t *e = &kv_A(st->data.entries, i);
      items[i].id = (int)i;
      items[i].icon = fm_logic_get_icon(e);
      items[i].label = e->name;

      if (strcmp(e->name, "..") == 0) {
        snprintf(st->details[i], sizeof(st->details[i]), "[Up]");
      } else if (fm_logic_is_dir(e)) {
        snprintf(st->details[i], sizeof(st->details[i]), "[DIR]");
      } else {
        fm_logic_format_size(e->stat.st_size, st->details[i], sizeof(st->details[i]));
      }
      items[i].detail = st->details[i];
    }
  }

  int top_offset = 0;
  if (!is_small) {
    int hdr_h = FM_HDR_HEIGHT_DEFAULT;
    top_offset = hdr_h + FM_BAR_HEIGHT;
  }

  eos_menu_cfg_t cfg = {
    .title = st->data.cwd,
    .icon = LV_SYMBOL_DIRECTORY,
    .items = items,
    .item_count = count,
    .on_select = fm_entry_selected_cb,
    .on_focus = fm_entry_focus_cb,
    .user_data = st,
    .headerless = false,
    .force_fullscreen = is_small,
    .width = is_small ? 0 : (int32_t)sw,
    .height = is_small ? (int32_t)sh : (int32_t)(sh - top_offset),
    .parent = st->scr,
  };

  st->menu_obj = eos_menu_create(&cfg);
  if (st->menu_obj) {
    if (!is_small) {
      lv_obj_align(st->menu_obj, LV_ALIGN_TOP_MID, 0, top_offset);
    }
    if (count > 1 && strcmp(kv_A(st->data.entries, 0).name, "..") == 0) {
      eos_menu_focus_index(st->menu_obj, 1);
    }
  }

  if (items) {
    free(items);
  }
}

static void fm_setup_ui(fm_state_t *st, uint32_t w, uint32_t h) {
  (void)w;
  st->scr = lv_scr_act();
  lv_obj_set_style_bg_color(st->scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_pad_all(st->scr, 0, 0);
  lv_obj_set_style_border_width(st->scr, 0, 0);

  bool is_small = eos_screen_is_compact();
  if (is_small) {
    // Small screens: pure menu mode (headerless) to maximize vertical space
    st->hdr = NULL;
    st->quick_bar = NULL;
    st->path_lbl = NULL;
    return;
  }

  int hdr_h = FM_HDR_HEIGHT_DEFAULT;

  // Header container
  st->hdr = lv_obj_create(st->scr);
  lv_obj_set_size(st->hdr, lv_pct(EOS_PERCENT_MAX), hdr_h);
  lv_obj_align(st->hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(st->hdr, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
  lv_obj_set_style_border_color(st->hdr, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(st->hdr, 1, 0);
  lv_obj_set_style_pad_all(st->hdr, 2, 0);

  // Up/Back button
  lv_obj_t *btn_up = lv_btn_create(st->hdr);
  lv_obj_set_size(btn_up, FM_BTN_ICON_W, FM_BTN_ICON_H);
  lv_obj_set_pos(btn_up, 2, 2);
  lv_obj_set_style_bg_color(btn_up, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_add_event_cb(btn_up, fm_back_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_up = lv_label_create(btn_up);
  lv_label_set_text(lbl_up, LV_SYMBOL_UP);
  lv_obj_center(lbl_up);

  st->path_lbl = lv_label_create(st->hdr);
  lv_label_set_text(st->path_lbl, "/");
  lv_obj_set_style_text_color(st->path_lbl, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
  lv_obj_set_pos(st->path_lbl, FM_BTN_ICON_W + 8, 8);

  // New folder button
  lv_obj_t *btn_new = lv_btn_create(st->hdr);
  lv_obj_set_size(btn_new, FM_BTN_ICON_W, FM_BTN_ICON_H);
  lv_obj_align(btn_new, LV_ALIGN_TOP_RIGHT, -(FM_BTN_ICON_W + 6), 2);
  lv_obj_set_style_bg_color(btn_new, lv_color_hex(EOS_THEME_ACCENT_GREEN), 0);
  lv_obj_add_event_cb(btn_new, fm_mkdir_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_new = lv_label_create(btn_new);
  lv_label_set_text(lbl_new, LV_SYMBOL_PLUS);
  lv_obj_center(lbl_new);

  // Refresh button
  lv_obj_t *btn_ref = lv_btn_create(st->hdr);
  lv_obj_set_size(btn_ref, FM_BTN_ICON_W, FM_BTN_ICON_H);
  lv_obj_align(btn_ref, LV_ALIGN_TOP_RIGHT, -2, 2);
  lv_obj_set_style_bg_color(btn_ref, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_add_event_cb(btn_ref, fm_refresh_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_ref = lv_label_create(btn_ref);
  lv_label_set_text(lbl_ref, LV_SYMBOL_REFRESH);
  lv_obj_center(lbl_ref);

  // Quick Jump Bar
  st->quick_bar = lv_obj_create(st->scr);
  lv_obj_set_size(st->quick_bar, lv_pct(EOS_PERCENT_MAX), FM_BAR_HEIGHT);
  lv_obj_align(st->quick_bar, LV_ALIGN_TOP_MID, 0, hdr_h);
  lv_obj_set_style_bg_color(st->quick_bar, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(st->quick_bar, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(st->quick_bar, 1, 0);
  lv_obj_set_style_pad_all(st->quick_bar, 2, 0);

  static const struct {
    const char *label;
    const char *path;
  } shortcuts[] = {
      {LV_SYMBOL_SD_CARD " SD", "/mnt/sd"},
      {LV_SYMBOL_HOME " Root", "/"},
      {LV_SYMBOL_PLAY " Bin", "/bin"},
      {LV_SYMBOL_DIRECTORY " Tmp", "/tmp"},
  };

  for (size_t i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); i++) {
    lv_obj_t *b = lv_btn_create(st->quick_bar);
    lv_obj_set_size(b, FM_BTN_SHORTCUT_W, FM_BTN_SHORTCUT_H);
    lv_obj_set_pos(b, (lv_coord_t)(i * (FM_BTN_SHORTCUT_W + 8) + 8), 2);
    lv_obj_set_style_bg_color(b, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_user_data(b, (void *)shortcuts[i].path);
    lv_obj_add_event_cb(b, fm_jump_btn_cb, LV_EVENT_CLICKED, st);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, shortcuts[i].label);
    lv_obj_center(lbl);
  }
}

static void fm_handle_key(fm_state_t *st, uint16_t key) {
  if (st->action_menu) {
    if (eos_menu_handle_key(st->action_menu, key)) {
      return;
    }
    if (key == KEY_B || key == KEY_ESC || key == KEY_BACKSPACE) {
      fm_close_action_menu(st);
    }
    return;
  }

  if (st->dialog) {
    if (eos_lvgl_dialog_handle_key(st->dialog, key)) {
      return;
    }
    if (key == KEY_B || key == KEY_ESC || key == KEY_BACKSPACE || key == KEY_A) {
      fm_close_dialog(st);
    }
    return;
  }

  if (key == KEY_C) {
    fm_trigger_mkdir(st);
    return;
  }

  if (key == KEY_D) {
    if (st->menu_obj) {
      int sel = eos_menu_get_selected_index(st->menu_obj);
      if (sel >= 0) {
        fm_show_file_actions(st, (size_t)sel);
      }
    }
    return;
  }

  if (key == KEY_B || key == KEY_BACKSPACE || key == KEY_ESC) {
    fm_navigate_up(st);
    return;
  }

  if (st->menu_obj) {
    if (eos_menu_handle_key(st->menu_obj, key)) {
      return;
    }
  }
}

int lvgl_fm_main(int argc, char **argv) {
  const char *start_path = NULL;
  const char *fb_path = NULL;
  const char *kbd_path = NULL;
  bool auto_mode = false;
  int timeout_ms = -1;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: lvgl_fm [options] [path]\n");
      printf("Options:\n");
      printf("  -h, --help       Show this help banner\n");
      printf("  -a, --auto       Automated self-test mode\n");
      printf("  -t, --timeout MS Timeout in milliseconds\n");
      return 0;
    } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto") == 0) {
      auto_mode = true;
      if (timeout_ms < 0) timeout_ms = EOS_MS_PER_SEC;
    } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) && i + 1 < argc) {
      timeout_ms = atoi(argv[++i]);
    } else if (argv[i][0] == '/') {
      start_path = argv[i];
    }
  }

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_fm: failed to init LVGL\n");
    return -1;
  }

  fm_state_t *st = calloc(1, sizeof(fm_state_t));
  if (!st) {
    eos_lvgl_deinit();
    return -1;
  }

  const char *initial_dir = "/";
  if (start_path && access(start_path, F_OK) == 0) {
    initial_dir = start_path;
  } else if (access("/mnt/sd", F_OK) == 0) {
    initial_dir = "/mnt/sd";
  }

  if (fm_logic_init(&st->data, initial_dir) != 0) {
    printf("lvgl_fm: failed to load initial directory %s\n", initial_dir);
  }

  st->auto_mode = auto_mode;
  st->timeout_ms = timeout_ms;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  fm_setup_ui(st, w, h);
  fm_render_file_list(st);

  (void)eos_toolbar_set("File Manager", "[▲▼]Move [A]Open [..]Up [D]Del [B]Exit");

  uint64_t start_ms = fm_get_time_ms();

  while (!st->quit) {
    if (st->pending_nav_path[0] != '\0') {
      char target[EOS_MID_STR_LEN];
      strlcpy(target, st->pending_nav_path, sizeof(target));
      st->pending_nav_path[0] = '\0';
      if (fm_logic_load_dir(&st->data, target) == 0) {
        fm_render_file_list(st);
        eos_lvgl_clear_keys();
      }
    }

    if (st->timeout_ms >= 0) {
      uint64_t elapsed = fm_get_time_ms() - start_ms;
      if (elapsed >= (uint64_t)st->timeout_ms) {
        break;
      }
    }

    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      fm_handle_key(st, key);
    }
    eos_lvgl_step(10);

    if (st->auto_mode) {
      break;
    }
  }

  (void)eos_toolbar_clear();

  if (st->auto_mode) {
    printf("lvgl_fm: self-test completed cleanly\n");
  }

  if (st->details) {
    free(st->details);
    st->details = NULL;
  }
  fm_logic_deinit(&st->data);
  free(st);

  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_fm_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_fm",
    .name = "File Manager",
    .group = "System",
    .description = "Minimalist graphical file manager with icons, delete, mkdir, and preview features",
    .entry_point = lvgl_fm_main,
};

EOS_BIN_ATTR const eos_bin_t fm_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "fm",
    .name = "File Manager (CLI)",
    .group = "System",
    .description = "Minimalist graphical file manager shortcut",
    .entry_point = lvgl_fm_main,
};
