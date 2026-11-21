#include "ebin/lvgl_fm/lvgl_fm.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static void fm_load_directory(fm_state_t *st, const char *path);
static void fm_setup_ui(fm_state_t *st, uint32_t w, uint32_t h);
static void fm_update_selection_highlight(fm_state_t *st);

static uint64_t get_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((uint64_t)tv.tv_sec * (uint64_t)EOS_MS_PER_SEC) +
         ((uint64_t)tv.tv_usec / (uint64_t)EOS_US_PER_MS);
}

static void fm_format_size(size_t bytes, char *buf, size_t buflen) {
  if (bytes < (size_t)FM_BYTES_PER_KB) {
    snprintf(buf, buflen, "%u B", (unsigned int)bytes);
  } else if (bytes < (size_t)FM_BYTES_PER_MB) {
    snprintf(buf, buflen, "%u KB", (unsigned int)(bytes / (size_t)FM_BYTES_PER_KB));
  } else {
    snprintf(buf, buflen, "%u MB", (unsigned int)(bytes / (size_t)FM_BYTES_PER_MB));
  }
}

static void fm_join_path(char *dst, size_t dst_size, const char *dir, const char *name) {
  if (!dst || dst_size == 0) return;
  if (!dir || strcmp(dir, "/") == 0) {
    snprintf(dst, dst_size, "/%s", name ? name : "");
    return;
  }
  strlcpy(dst, dir, dst_size);
  strlcat(dst, "/", dst_size);
  strlcat(dst, name ? name : "", dst_size);
}

static void fm_action_btn_cb(lv_event_t *e) {
  fm_state_t *st = (fm_state_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  const char *act = (const char *)lv_obj_get_user_data(btn);

  if (!st || !act) return;

  if (strcmp(act, "cancel") == 0) {
    if (st->dialog) {
      lv_obj_del_async(st->dialog);
      st->dialog = NULL;
    }
    return;
  }

  if (strcmp(act, "open") == 0) {
    if (st->selected_is_dir) {
      char target_path[EOS_MID_STR_LEN];
      strlcpy(target_path, st->selected_path, sizeof(target_path));
      if (st->dialog) {
        lv_obj_del_async(st->dialog);
        st->dialog = NULL;
      }
      strlcpy(st->pending_nav_path, target_path, sizeof(st->pending_nav_path));
    } else {
      if (st->dialog) {
        lv_obj_del_async(st->dialog);
        st->dialog = NULL;
      }
      printf("[fm] opening: %s\n", st->selected_path);
      (void)eos_toolbar_printf(NULL, "Opening %s", st->selected_name);
    }
    return;
  }

  if (strcmp(act, "delete") == 0) {
    int rc = -1;
    if (st->selected_is_dir) {
      rc = rmdir(st->selected_path);
    } else {
      rc = unlink(st->selected_path);
    }
    if (st->dialog) {
      lv_obj_del_async(st->dialog);
      st->dialog = NULL;
    }
    if (rc == 0) {
      (void)eos_toolbar_printf(NULL, "Deleted %s", st->selected_name);
    } else {
      (void)eos_toolbar_printf(NULL, "Failed to delete: %s", strerror(errno));
    }
    strlcpy(st->pending_nav_path, st->cwd, sizeof(st->pending_nav_path));
    return;
  }

  if (strcmp(act, "view") == 0) {
    char target_path[EOS_MID_STR_LEN];
    strlcpy(target_path, st->selected_path, sizeof(target_path));
    if (st->dialog) {
      lv_obj_del_async(st->dialog);
      st->dialog = NULL;
    }

    int fd = open(target_path, O_RDONLY);
    char *view_buf = calloc(1, FM_VIEW_MAX_BYTES + 1);
    ssize_t n = 0;
    if (fd >= 0) {
      if (view_buf) {
        n = read(fd, view_buf, FM_VIEW_MAX_BYTES);
        if (n >= 0) view_buf[n] = '\0';
      }
      close(fd);
    }

    lv_obj_t *box = lv_obj_create(st->scr);
    lv_obj_set_size(box, lv_pct(FM_VIEW_MODAL_W_PCT), lv_pct(FM_VIEW_MODAL_H_PCT));
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(EOS_THEME_INFO_BLUE), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_pad_all(box, 8, 0);

    lv_obj_t *v_title = lv_label_create(box);
    lv_label_set_text_fmt(v_title, LV_SYMBOL_FILE "  %s", st->selected_name);
    lv_obj_set_style_text_color(v_title, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
    lv_obj_align(v_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *txt = lv_textarea_create(box);
    lv_obj_set_size(txt, lv_pct(100), lv_pct(70));
    lv_obj_align(txt, LV_ALIGN_TOP_MID, 0, 24);
    lv_textarea_set_text(txt, view_buf ? view_buf : "Error reading file");
    lv_obj_set_style_bg_color(txt, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
    lv_obj_set_style_text_color(txt, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
    lv_obj_set_style_border_width(txt, 0, 0);

    lv_obj_t *c_btn = lv_btn_create(box);
    lv_obj_align(c_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(c_btn, lv_pct(FM_VIEW_CLOSE_BTN_W_PCT), 28);
    lv_obj_set_user_data(c_btn, (void *)"cancel");
    lv_obj_add_event_cb(c_btn, fm_action_btn_cb, LV_EVENT_CLICKED, st);
    lv_obj_t *c_lbl = lv_label_create(c_btn);
    lv_label_set_text(c_lbl, LV_SYMBOL_CLOSE " Close");
    lv_obj_center(c_lbl);

    free(view_buf);
    st->dialog = box;
    return;
  }
}

static void fm_show_file_actions(fm_state_t *st, const char *name, bool is_dir) {
  if (st->dialog) {
    lv_obj_del_async(st->dialog);
    st->dialog = NULL;
  }

  strlcpy(st->selected_name, name, sizeof(st->selected_name));
  st->selected_is_dir = is_dir;
  fm_join_path(st->selected_path, sizeof(st->selected_path), st->cwd, name);

  lv_obj_t *box = lv_obj_create(st->scr);
  lv_obj_set_size(box, FM_ACTIONS_MODAL_W, LV_SIZE_CONTENT);
  lv_obj_center(box);
  lv_obj_set_style_bg_color(box, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_radius(box, 6, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_style_clip_corner(box, true, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 0, 0);
  lv_obj_set_style_pad_column(box, 0, 0);

  // Header banner with file/folder title
  lv_obj_t *hdr = lv_obj_create(box);
  lv_obj_set_size(hdr, lv_pct(100), FM_ACTIONS_BTN_H);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
  lv_obj_set_style_border_color(hdr, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(hdr, 1, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_hor(hdr, 6, 0);
  lv_obj_set_style_pad_ver(hdr, 0, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *title = lv_label_create(hdr);
  const char *icon = is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
  lv_label_set_text_fmt(title, "%s  %s", icon, name);
  lv_obj_set_style_text_color(title, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, lv_pct(100));
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  // 1. Open
  lv_obj_t *btn_open = lv_btn_create(box);
  lv_obj_set_size(btn_open, lv_pct(100), FM_ACTIONS_BTN_H);
  lv_obj_set_style_radius(btn_open, 0, 0);
  lv_obj_set_style_bg_color(btn_open, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(btn_open, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(btn_open, 1, 0);
  lv_obj_set_style_border_side(btn_open, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_pad_all(btn_open, 0, 0);
  lv_obj_set_user_data(btn_open, (void *)"open");
  lv_obj_add_event_cb(btn_open, fm_action_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_o = lv_label_create(btn_open);
  lv_label_set_text(lbl_o, is_dir ? LV_SYMBOL_DIRECTORY "  Open" : LV_SYMBOL_PLAY "  Open");
  lv_obj_center(lbl_o);

  // 2. View / Preview (for files only)
  if (!is_dir) {
    lv_obj_t *btn_view = lv_btn_create(box);
    lv_obj_set_size(btn_view, lv_pct(100), FM_ACTIONS_BTN_H);
    lv_obj_set_style_radius(btn_view, 0, 0);
    lv_obj_set_style_bg_color(btn_view, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
    lv_obj_set_style_border_color(btn_view, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
    lv_obj_set_style_border_width(btn_view, 1, 0);
    lv_obj_set_style_border_side(btn_view, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(btn_view, 0, 0);
    lv_obj_set_user_data(btn_view, (void *)"view");
    lv_obj_add_event_cb(btn_view, fm_action_btn_cb, LV_EVENT_CLICKED, st);
    lv_obj_t *lbl_v = lv_label_create(btn_view);
    lv_label_set_text(lbl_v, LV_SYMBOL_EYE_OPEN "  Preview");
    lv_obj_center(lbl_v);
  }

  // 3. Delete
  lv_obj_t *btn_del = lv_btn_create(box);
  lv_obj_set_size(btn_del, lv_pct(100), FM_ACTIONS_BTN_H);
  lv_obj_set_style_radius(btn_del, 0, 0);
  lv_obj_set_style_bg_color(btn_del, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(btn_del, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(btn_del, 1, 0);
  lv_obj_set_style_border_side(btn_del, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_pad_all(btn_del, 0, 0);
  lv_obj_set_user_data(btn_del, (void *)"delete");
  lv_obj_add_event_cb(btn_del, fm_action_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_d = lv_label_create(btn_del);
  lv_label_set_text(lbl_d, LV_SYMBOL_TRASH "  Delete");
  lv_obj_set_style_text_color(lbl_d, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
  lv_obj_center(lbl_d);

  // 4. Cancel
  lv_obj_t *btn_can = lv_btn_create(box);
  lv_obj_set_size(btn_can, lv_pct(100), FM_ACTIONS_BTN_H);
  lv_obj_set_style_radius(btn_can, 0, 0);
  lv_obj_set_style_bg_color(btn_can, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_width(btn_can, 0, 0);
  lv_obj_set_style_pad_all(btn_can, 0, 0);
  lv_obj_set_user_data(btn_can, (void *)"cancel");
  lv_obj_add_event_cb(btn_can, fm_action_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_c = lv_label_create(btn_can);
  lv_label_set_text(lbl_c, LV_SYMBOL_CLOSE "  Cancel");
  lv_obj_center(lbl_c);

  st->dialog = box;
}

static void fm_mkdir_confirm_cb(const char *name, void *user_data) {
  fm_state_t *st = (fm_state_t *)user_data;
  if (!st || !name || name[0] == '\0') return;

  char full[EOS_MID_STR_LEN];
  fm_join_path(full, sizeof(full), st->cwd, name);

  if (mkdir(full, EOS_DEFAULT_DIR_MODE) == 0) {
    (void)eos_toolbar_printf(NULL, "Created %s", name);
    strlcpy(st->pending_nav_path, st->cwd, sizeof(st->pending_nav_path));
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
  strlcpy(st->pending_nav_path, st->cwd, sizeof(st->pending_nav_path));
}

static void fm_navigate_up(fm_state_t *st) {
  if (!st) return;

  if (strcmp(st->cwd, "/") == 0) {
    st->quit = true;
    return;
  }

  char *last_slash = strrchr(st->cwd, '/');
  if (last_slash && last_slash != st->cwd) {
    *last_slash = '\0';
  } else {
    strlcpy(st->cwd, "/", sizeof(st->cwd));
  }
  strlcpy(st->pending_nav_path, st->cwd, sizeof(st->pending_nav_path));
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

static void fm_row_click_cb(lv_event_t *e) {
  fm_state_t *st = (fm_state_t *)lv_event_get_user_data(e);
  int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));

  // Special index -1 means parent ".."
  if (idx == -1) {
    fm_back_btn_cb(e);
    return;
  }

  if (idx < 0 || idx >= st->entry_count) return;

  fm_entry_t *entry = &st->entries[idx];
  if (entry->is_dir) {
    char new_path[EOS_MID_STR_LEN];
    fm_join_path(new_path, sizeof(new_path), st->cwd, entry->name);
    strlcpy(st->pending_nav_path, new_path, sizeof(st->pending_nav_path));
  } else {
    fm_show_file_actions(st, entry->name, false);
  }
}

static void fm_update_selection_highlight(fm_state_t *st) {
  for (int i = 0; i < st->list_btns_count; i++) {
    if (!st->list_btns[i]) continue;
    if (i == st->selected_idx) {
      lv_obj_set_style_bg_color(st->list_btns[i], lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
      lv_obj_set_style_border_color(st->list_btns[i], lv_color_hex(EOS_THEME_INFO_BLUE), 0);
      lv_obj_set_style_border_width(st->list_btns[i], 1, 0);
      lv_obj_scroll_to_view(st->list_btns[i], LV_ANIM_OFF);
    } else {
      lv_obj_set_style_bg_color(st->list_btns[i], lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
      lv_obj_set_style_border_width(st->list_btns[i], 0, 0);
    }
  }
}

static void fm_load_directory(fm_state_t *st, const char *path) {
  DIR *d = opendir(path);
  if (!d) {
    printf("fm: failed to open directory: %s\n", path);
    return;
  }

  strlcpy(st->cwd, path, sizeof(st->cwd));
  if (st->path_lbl) {
    lv_label_set_text(st->path_lbl, st->cwd);
  }

  st->entry_count = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
      continue;
    }
    if (st->entry_count >= FM_MAX_ENTRIES) {
      break;
    }

    fm_entry_t *ent = &st->entries[st->entry_count];
    memset(ent, 0, sizeof(fm_entry_t));
    strlcpy(ent->name, de->d_name, sizeof(ent->name));

    char full_p[EOS_MID_STR_LEN];
    fm_join_path(full_p, sizeof(full_p), st->cwd, de->d_name);

    struct stat sb;
    if (stat(full_p, &sb) == 0) {
      ent->is_dir = S_ISDIR(sb.st_mode);
      ent->size = (size_t)sb.st_size;
      ent->is_exec = (sb.st_mode & S_IXUSR) || (strncmp(st->cwd, "/bin", 4) == 0);
    } else {
      ent->is_dir = false;
      ent->size = 0;
    }
    st->entry_count++;
  }
  closedir(d);

  // Sort: directories first (alphabetical), then files (alphabetical)
  for (int i = 0; i < st->entry_count - 1; i++) {
    for (int j = 0; j < st->entry_count - i - 1; j++) {
      bool swap = false;
      if (st->entries[j].is_dir != st->entries[j + 1].is_dir) {
        swap = !st->entries[j].is_dir && st->entries[j + 1].is_dir;
      } else {
        swap = (strcmp(st->entries[j].name, st->entries[j + 1].name) > 0);
      }
      if (swap) {
        fm_entry_t tmp = st->entries[j];
        st->entries[j] = st->entries[j + 1];
        st->entries[j + 1] = tmp;
      }
    }
  }

  // Clear existing list items
  lv_obj_clean(st->list);
  st->list_btns_count = 0;
  st->selected_idx = 0;

  // Add parent ".." entry if not at root
  if (strcmp(st->cwd, "/") != 0) {
    lv_obj_t *up_btn = lv_list_add_btn(st->list, LV_SYMBOL_UP, "..");
    lv_obj_set_style_bg_color(up_btn, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
    lv_obj_set_style_text_color(up_btn, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
    lv_obj_set_user_data(up_btn, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(up_btn, fm_row_click_cb, LV_EVENT_CLICKED, st);
    if (st->list_btns_count < FM_MAX_ENTRIES) {
      st->list_btns[st->list_btns_count++] = up_btn;
    }
  }

  // If directory is empty, show clean label
  if (st->entry_count == 0) {
    lv_obj_t *empty_lbl = lv_label_create(st->list);
    lv_label_set_text(empty_lbl, LV_SYMBOL_DIRECTORY "  (empty)");
    lv_obj_set_style_text_color(empty_lbl, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
    lv_obj_set_style_pad_all(empty_lbl, 16, 0);
  }

  for (int i = 0; i < st->entry_count; i++) {
    fm_entry_t *ent = &st->entries[i];
    const char *icon = ent->is_dir ? LV_SYMBOL_DIRECTORY : (ent->is_exec ? LV_SYMBOL_PLAY : LV_SYMBOL_FILE);
    uint32_t text_color = ent->is_dir ? EOS_THEME_TEXT_PRIMARY
                                      : (ent->is_exec ? EOS_THEME_ACCENT_GREEN : EOS_THEME_TEXT_PRIMARY);

    char row_txt[EOS_MID_STR_LEN];
    if (ent->is_dir) {
      snprintf(row_txt, sizeof(row_txt), "%s", ent->name);
    } else {
      char sz_buf[EOS_STR_20_LEN];
      fm_format_size(ent->size, sz_buf, sizeof(sz_buf));
      snprintf(row_txt, sizeof(row_txt), "%s   %s", ent->name, sz_buf);
    }

    lv_obj_t *btn = lv_list_add_btn(st->list, icon, row_txt);
    lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(text_color), 0);
    lv_obj_set_user_data(btn, (void *)(intptr_t)i);
    lv_obj_add_event_cb(btn, fm_row_click_cb, LV_EVENT_CLICKED, st);

    if (st->list_btns_count < FM_MAX_ENTRIES) {
      st->list_btns[st->list_btns_count++] = btn;
    }
  }

  fm_update_selection_highlight(st);
}

static void fm_setup_ui(fm_state_t *st, uint32_t w, uint32_t h) {
  (void)w;
  st->scr = lv_scr_act();
  lv_obj_set_style_bg_color(st->scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);

  bool compact = (h <= EOS_LVGL_DIALOG_MIN_W);
  int hdr_h = compact ? FM_HDR_HEIGHT_COMPACT : FM_HDR_HEIGHT_DEFAULT;

  // Header container
  st->hdr = lv_obj_create(st->scr);
  lv_obj_set_size(st->hdr, lv_pct(100), hdr_h);
  lv_obj_align(st->hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(st->hdr, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
  lv_obj_set_style_border_color(st->hdr, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(st->hdr, 1, 0);
  lv_obj_set_style_pad_all(st->hdr, 2, 0);
  lv_obj_set_flex_flow(st->hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(st->hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Up/Back button
  lv_obj_t *btn_up = lv_btn_create(st->hdr);
  lv_obj_set_size(btn_up, FM_BTN_ICON_W, FM_BTN_ICON_H);
  lv_obj_set_style_bg_color(btn_up, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_add_event_cb(btn_up, fm_back_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_up = lv_label_create(btn_up);
  lv_label_set_text(lbl_up, LV_SYMBOL_UP);
  lv_obj_center(lbl_up);

  st->path_lbl = lv_label_create(st->hdr);
  lv_label_set_text(st->path_lbl, "/");
  lv_obj_set_style_text_color(st->path_lbl, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
  lv_obj_set_flex_grow(st->path_lbl, 1);
  lv_label_set_long_mode(st->path_lbl, LV_LABEL_LONG_DOT);

  // New folder button
  lv_obj_t *btn_new = lv_btn_create(st->hdr);
  lv_obj_set_size(btn_new, FM_BTN_ICON_W, FM_BTN_ICON_H);
  lv_obj_set_style_bg_color(btn_new, lv_color_hex(EOS_THEME_ACCENT_GREEN), 0);
  lv_obj_add_event_cb(btn_new, fm_mkdir_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_new = lv_label_create(btn_new);
  lv_label_set_text(lbl_new, LV_SYMBOL_PLUS);
  lv_obj_center(lbl_new);

  // Refresh button
  lv_obj_t *btn_ref = lv_btn_create(st->hdr);
  lv_obj_set_size(btn_ref, FM_BTN_ICON_W, FM_BTN_ICON_H);
  lv_obj_set_style_bg_color(btn_ref, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_add_event_cb(btn_ref, fm_refresh_btn_cb, LV_EVENT_CLICKED, st);
  lv_obj_t *lbl_ref = lv_label_create(btn_ref);
  lv_label_set_text(lbl_ref, LV_SYMBOL_REFRESH);
  lv_obj_center(lbl_ref);

  // Quick Jump Bar
  lv_obj_t *bar = lv_obj_create(st->scr);
  lv_obj_set_size(bar, lv_pct(100), FM_BAR_HEIGHT);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, hdr_h);
  lv_obj_set_style_bg_color(bar, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_pad_all(bar, 2, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  const struct {
    const char *label;
    const char *path;
  } shortcuts[] = {
      {LV_SYMBOL_SD_CARD " SD", "/mnt/sd"},
      {LV_SYMBOL_HOME " Root", "/"},
      {LV_SYMBOL_PLAY " Bin", "/bin"},
      {LV_SYMBOL_DRIVE " Tmp", "/tmp"},
  };

  for (size_t i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); i++) {
    lv_obj_t *b = lv_btn_create(bar);
    lv_obj_set_size(b, FM_BTN_SHORTCUT_W, FM_BTN_SHORTCUT_H);
    lv_obj_set_style_bg_color(b, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_user_data(b, (void *)shortcuts[i].path);
    lv_obj_add_event_cb(b, fm_jump_btn_cb, LV_EVENT_CLICKED, st);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, shortcuts[i].label);
    lv_obj_center(lbl);
  }

  // Scrollable list container
  st->list = lv_list_create(st->scr);
  int top_offset = hdr_h + FM_BAR_HEIGHT;
  lv_obj_set_size(st->list, lv_pct(100), h - top_offset);
  lv_obj_align(st->list, LV_ALIGN_TOP_MID, 0, top_offset);
  lv_obj_set_style_bg_color(st->list, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_border_width(st->list, 0, 0);
  lv_obj_set_style_pad_all(st->list, 2, 0);
}

static void fm_handle_key(fm_state_t *st, uint16_t key) {
  if (st->dialog) {
    if (key == KEY_B || key == KEY_ESC || key == KEY_BACKSPACE) {
      lv_obj_del_async(st->dialog);
      st->dialog = NULL;
    }
    return;
  }

  switch (key) {
  case KEY_UP:
    if (st->selected_idx > 0) {
      st->selected_idx--;
      fm_update_selection_highlight(st);
    }
    break;
  case KEY_DOWN:
    if (st->selected_idx < st->list_btns_count - 1) {
      st->selected_idx++;
      fm_update_selection_highlight(st);
    }
    break;
  case KEY_A:
  case KEY_ENTER:
    if (st->list_btns_count > 0 && st->selected_idx >= 0 &&
        st->selected_idx < st->list_btns_count && st->list_btns[st->selected_idx]) {
      lv_obj_send_event(st->list_btns[st->selected_idx], LV_EVENT_CLICKED, NULL);
    }
    break;
  case KEY_B:
  case KEY_BACKSPACE:
  case KEY_ESC:
    fm_navigate_up(st);
    break;
  case KEY_C:
    fm_trigger_mkdir(st);
    break;
  case KEY_D:
    if (st->list_btns_count > 0 && st->selected_idx >= 0 &&
        st->selected_idx < st->list_btns_count && st->list_btns[st->selected_idx]) {
      int idx = (int)(intptr_t)lv_obj_get_user_data(st->list_btns[st->selected_idx]);
      if (idx >= 0 && idx < st->entry_count) {
        fm_entry_t *ent = &st->entries[idx];
        fm_show_file_actions(st, ent->name, ent->is_dir);
      }
    }
    break;
  default:
    break;
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

  st->entries = calloc(FM_MAX_ENTRIES, sizeof(fm_entry_t));
  st->list_btns = calloc(FM_MAX_ENTRIES + 1, sizeof(lv_obj_t *));
  if (!st->entries || !st->list_btns) {
    free(st->entries);
    free(st->list_btns);
    free(st);
    eos_lvgl_deinit();
    return -1;
  }

  st->auto_mode = auto_mode;
  st->timeout_ms = timeout_ms;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  fm_setup_ui(st, w, h);

  const char *initial_dir = "/";
  if (start_path && access(start_path, F_OK) == 0) {
    initial_dir = start_path;
  } else if (access("/mnt/sd", F_OK) == 0) {
    initial_dir = "/mnt/sd";
  }

  fm_load_directory(st, initial_dir);

  (void)eos_toolbar_set("File Manager", "[▲▼]Move [A]Open [..]Up [D]Del [B]Exit");

  uint64_t start_ms = get_time_ms();

  while (!st->quit) {
    if (st->pending_nav_path[0] != '\0') {
      char target[EOS_MID_STR_LEN];
      strlcpy(target, st->pending_nav_path, sizeof(target));
      st->pending_nav_path[0] = '\0';
      fm_load_directory(st, target);
    }

    if (st->timeout_ms >= 0) {
      uint64_t elapsed = get_time_ms() - start_ms;
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

  free(st->entries);
  free(st->list_btns);
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
