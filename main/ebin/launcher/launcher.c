#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ecore/proc.h"
#include "ebin/launcher/launcher.h"
#include "emisc/fancymacro.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
  char name[EOS_XSMALL_STR_LEN];
  int count;
} launcher_group_t;

typedef struct {
  const eos_bin_t *bins[LAUNCHER_MAX_BINS];
  int bin_count;

  launcher_group_t groups[LAUNCHER_MAX_GROUPS];
  int group_count;

  const eos_bin_t *filtered[LAUNCHER_MAX_BINS];
  int filtered_count;

  int selected_group; // 0 = "All", 1..group_count-1 = specific group
  int selected_item;  // index in filtered[]

  int last_exit_code;
  char last_launched[EOS_SMALL_STR_LEN];
  char custom_cmd[EOS_MID_STR_LEN];
  bool has_launched;
  bool should_exit;
  bool trigger_launch;

  // LVGL widgets
  lv_obj_t *hdr_title;
  lv_obj_t *grp_bar;
  lv_obj_t *grp_label;
  lv_obj_t *list_cont;
  lv_obj_t *item_btns[LAUNCHER_MAX_BINS];
  lv_obj_t *info_title;
  lv_obj_t *info_desc;
  lv_obj_t *desc_dialog;
} launcher_state_t;

static const char *bin_effective_group(const eos_bin_t *bin) {
  if (bin && bin->group[0] != '\0') {
    return bin->group;
  }
  return LAUNCHER_DEFAULT_GROUP;
}

static const char *bin_effective_name(const eos_bin_t *bin) {
  if (bin && bin->name[0] != '\0') {
    return bin->name;
  }
  if (bin && bin->filename[0] != '\0') {
    return bin->filename;
  }
  return "unnamed";
}

static const char *bin_effective_desc(const eos_bin_t *bin) {
  if (bin && bin->description[0] != '\0') {
    return bin->description;
  }
  return "EOS native binary application";
}

// Returns true if the binary manages /dev/fb0 directly (EOS_BIN_FLAG_GUI, LVGL, SDL,
// or raw display) or is an interactive shell; returns false for CLI/terminal tasks
// whose stdout/stderr should be piped to LAUNCHER_TERM_OUT_FILE and displayed in lvgl_term.
static bool bin_is_direct_display_or_shell(const eos_bin_t *bin) {
  if (!bin || !bin->filename[0])
    return false;
  if (bin->flags & EOS_BIN_FLAG_GUI)
    return true;
  const char *fn = bin->filename;
  if (strncmp(fn, "lvgl_", 5) == 0)
    return true;
  if (strcmp(fn, "test_lvgl") == 0)
    return true;
  if (strncmp(fn, "test_disp_", 10) == 0)
    return true;
  if (strncmp(fn, "test_sdl", 8) == 0)
    return true;
  if (strcmp(fn, "ush") == 0 || strcmp(fn, "launcher") == 0)
    return true;
  return false;
}

static int find_or_add_group(launcher_state_t *st, const char *group_name) {
  for (int i = 1; i < st->group_count; i++) {
    if (strcasecmp(st->groups[i].name, group_name) == 0) {
      return i;
    }
  }
  if (st->group_count >= LAUNCHER_MAX_GROUPS) {
    return 1;
  }
  int idx = st->group_count++;
  snprintf(st->groups[idx].name, sizeof(st->groups[idx].name), "%s",
           group_name);
  st->groups[idx].count = 0;
  return idx;
}

static void launcher_discover_bins(launcher_state_t *st, bool include_hidden) {
  st->bin_count = 0;
  st->group_count = 1;
  snprintf(st->groups[0].name, sizeof(st->groups[0].name), "All");
  st->groups[0].count = 0;

  for (const eos_bin_t *b = _eos_bins_start; b < _eos_bins_end; b++) {
    if (b->magic != EOS_BIN_MAGIC || !b->entry_point || b->filename[0] == '\0')
      continue;
    if (!include_hidden && (b->flags & EOS_BIN_FLAG_HIDDEN))
      continue;
    if (strcmp(b->filename, "launcher") == 0)
      continue;
    if (st->bin_count >= LAUNCHER_MAX_BINS)
      break;

    st->bins[st->bin_count++] = b;
    const char *gname = bin_effective_group(b);
    int gidx = find_or_add_group(st, gname);
    st->groups[gidx].count++;
    st->groups[0].count++;
  }

  // Sort groups[1..group_count-1] alphabetically (keeping "System" last)
  for (int i = 1; i < st->group_count - 1; i++) {
    for (int j = i + 1; j < st->group_count; j++) {
      bool i_sys = (strcasecmp(st->groups[i].name, LAUNCHER_DEFAULT_GROUP) == 0);
      bool j_sys = (strcasecmp(st->groups[j].name, LAUNCHER_DEFAULT_GROUP) == 0);
      int cmp = strcasecmp(st->groups[i].name, st->groups[j].name);
      if ((i_sys && !j_sys) || (i_sys == j_sys && cmp > 0)) {
        launcher_group_t tmp = st->groups[i];
        st->groups[i] = st->groups[j];
        st->groups[j] = tmp;
      }
    }
  }

  // Sort bins by group order then by UI name so "All" view is neatly grouped
  for (int i = 0; i < st->bin_count - 1; i++) {
    for (int j = i + 1; j < st->bin_count; j++) {
      const char *gi = bin_effective_group(st->bins[i]);
      const char *gj = bin_effective_group(st->bins[j]);
      int gcmp = strcasecmp(gi, gj);
      bool gi_sys = (strcasecmp(gi, LAUNCHER_DEFAULT_GROUP) == 0);
      bool gj_sys = (strcasecmp(gj, LAUNCHER_DEFAULT_GROUP) == 0);
      bool swap = false;
      if (gi_sys != gj_sys) {
        swap = gi_sys;
      } else if (gcmp != 0) {
        swap = (gcmp > 0);
      } else {
        swap = (strcasecmp(bin_effective_name(st->bins[i]),
                           bin_effective_name(st->bins[j])) > 0);
      }
      if (swap) {
        const eos_bin_t *tmp = st->bins[i];
        st->bins[i] = st->bins[j];
        st->bins[j] = tmp;
      }
    }
  }
}

static void launcher_print_grouped_list(const launcher_state_t *st) {
  printf("EOS Launcher — %d applications in %d groups:\n", st->bin_count,
         st->group_count - 1);
  for (int g = 1; g < st->group_count; g++) {
    printf("\n[%s] (%d)\n", st->groups[g].name, st->groups[g].count);
    for (int i = 0; i < st->bin_count; i++) {
      const eos_bin_t *b = st->bins[i];
      if (strcasecmp(bin_effective_group(b), st->groups[g].name) == 0) {
        printf("  %-18s %-20s [stk=%u%s] %s\n", b->filename,
               bin_effective_name(b),
               (unsigned)eos_bin_effective_stack_size(b),
               (b->flags & EOS_BIN_FLAG_HIDDEN) ? ",hidden" : "",
               bin_effective_desc(b));
      }
    }
  }
}

static void launcher_populate_list(launcher_state_t *st);

static void launcher_update_details(launcher_state_t *st) {
  if (!st)
    return;

  if (st->grp_label && st->group_count > 0) {
    const launcher_group_t *grp = &st->groups[st->selected_group];
    char gbuf[EOS_LINE_64_LEN];
    snprintf(gbuf, sizeof(gbuf), "<  %s (%d)  >   [%d/%d]", grp->name,
             grp->count, st->selected_group + 1, st->group_count);
    lv_label_set_text(st->grp_label, gbuf);
  }

  for (int i = 0; i < st->filtered_count; i++) {
    lv_obj_t *btn = st->item_btns[i];
    if (!btn)
      continue;
    if (i == st->selected_item) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PRIMARY_BLUE), 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(EOS_COLOR_BORDER_FOCUS), 0);
      lv_obj_set_style_border_width(btn, 2, 0);
      lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
    } else {
      lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
      lv_obj_set_style_border_width(btn, 1, 0);
    }
  }

  if (st->filtered_count > 0 && st->selected_item >= 0 &&
      st->selected_item < st->filtered_count) {
    const eos_bin_t *b = st->filtered[st->selected_item];
    if (st->info_title && st->info_desc) {
      char tbuf[EOS_STR_96_LEN];
      snprintf(tbuf, sizeof(tbuf), "%s (/bin/%s) [%s]", bin_effective_name(b),
               b->filename, bin_effective_group(b));
      lv_label_set_text(st->info_title, tbuf);
      lv_label_set_text(st->info_desc, bin_effective_desc(b));
    }

    eos_toolbar_set(bin_effective_name(b),
                    (eos_lvgl_get_height() <= LAUNCHER_COMPACT_HEIGHT_LIMIT)
                        ? "[▲▼]Nav  [◄►]Tab  [A]Run  [D]Info"
                        : "[▲▼]Nav  [◄►]Tab  [A]Run");
  } else {
    if (st->info_title && st->info_desc) {
      lv_label_set_text(st->info_title, "No applications in group");
      lv_label_set_text(st->info_desc, "Use LEFT/RIGHT to switch group");
    }

    eos_toolbar_set("Launcher", "[▲▼]Nav  [◄►]Tab  [A]Run");
  }
}

static void launcher_toggle_desc(launcher_state_t *st) {
  if (st->desc_dialog) {
    lv_obj_delete(st->desc_dialog);
    st->desc_dialog = NULL;
    return;
  }

  if (st->filtered_count <= 0 || st->selected_item < 0 ||
      st->selected_item >= st->filtered_count) {
    return;
  }

  const eos_bin_t *b = st->filtered[st->selected_item];
  if (!b) return;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();
  lv_obj_t *scr = lv_screen_active();

  st->desc_dialog = lv_obj_create(scr);
  lv_obj_set_size(st->desc_dialog, (int32_t)w - 16, (int32_t)h - 16);
  lv_obj_center(st->desc_dialog);
  lv_obj_set_style_bg_color(st->desc_dialog, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(st->desc_dialog, lv_color_hex(EOS_THEME_INFO_BLUE), 0);
  lv_obj_set_style_border_width(st->desc_dialog, 2, 0);
  lv_obj_set_style_radius(st->desc_dialog, 6, 0);
  lv_obj_set_style_pad_all(st->desc_dialog, 8, 0);
  lv_obj_clear_flag(st->desc_dialog, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t = lv_label_create(st->desc_dialog);
  char tbuf[EOS_STR_96_LEN];
  snprintf(tbuf, sizeof(tbuf), "%s", bin_effective_name(b));
  lv_label_set_text(t, tbuf);
  lv_obj_set_style_text_color(t, lv_color_hex(EOS_THEME_INFO_BLUE), 0);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *fn = lv_label_create(st->desc_dialog);
  char fnbuf[EOS_LINE_64_LEN];
  snprintf(fnbuf, sizeof(fnbuf), "/bin/%s  [%s]", b->filename, bin_effective_group(b));
  lv_label_set_text(fn, fnbuf);
  lv_obj_set_style_text_color(fn, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
  lv_obj_align(fn, LV_ALIGN_TOP_LEFT, 0, 18);

  lv_obj_t *d = lv_label_create(st->desc_dialog);
  lv_label_set_text(d, bin_effective_desc(b));
  lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(d, (int32_t)w - LAUNCHER_DESC_DIALOG_PAD);
  lv_obj_set_style_text_color(d, lv_color_hex(EOS_COLOR_TEXT_SECONDARY), 0);
  lv_obj_align(d, LV_ALIGN_TOP_LEFT, 0, 38);

  lv_obj_t *hint = lv_label_create(st->desc_dialog);
  lv_label_set_text(hint, "[D or B to Close]");
  lv_obj_set_style_text_color(hint, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void launcher_item_event_cb(lv_event_t *e) {
  launcher_state_t *st = (launcher_state_t *)lv_event_get_user_data(e);
  if (!st) return;
  lv_obj_t *target = lv_event_get_current_target(e);
  for (int i = 0; i < st->filtered_count; i++) {
    if (st->item_btns[i] == target) {
      st->selected_item = i;
      launcher_update_details(st);
      st->trigger_launch = true;
      break;
    }
  }
}

static void launcher_grp_event_cb(lv_event_t *e) {
  launcher_state_t *st = (launcher_state_t *)lv_event_get_user_data(e);
  if (!st) return;
  if (st->group_count > 1) {
    st->selected_group = (st->selected_group + 1) % st->group_count;
    st->selected_item = 0;
    launcher_populate_list(st);
  }
}

static void launcher_gesture_cb(lv_event_t *e) {
  launcher_state_t *st = (launcher_state_t *)lv_event_get_user_data(e);
  if (!st) return;
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_LEFT) {
    if (st->group_count > 1) {
      st->selected_group = (st->selected_group + 1) % st->group_count;
      st->selected_item = 0;
      launcher_populate_list(st);
    }
  } else if (dir == LV_DIR_RIGHT) {
    if (st->group_count > 1) {
      st->selected_group =
          (st->selected_group - 1 + st->group_count) % st->group_count;
      st->selected_item = 0;
      launcher_populate_list(st);
    }
  }
}

static void launcher_populate_list(launcher_state_t *st) {
  if (!st->list_cont)
    return;

  lv_obj_clean(st->list_cont);
  memset(st->item_btns, 0, sizeof(st->item_btns));
  st->filtered_count = 0;

  const char *target_grp =
      (st->selected_group > 0 && st->selected_group < st->group_count)
          ? st->groups[st->selected_group].name
          : NULL;

  const char *last_grp = NULL;
  for (int i = 0; i < st->bin_count; i++) {
    const eos_bin_t *b = st->bins[i];
    const char *gname = bin_effective_group(b);
    if (target_grp && strcasecmp(gname, target_grp) != 0)
      continue;

    // When showing "All", insert a group header row before each new group
    if (!target_grp && (!last_grp || strcasecmp(last_grp, gname) != 0)) {
      last_grp = gname;
      lv_obj_t *ghdr = lv_obj_create(st->list_cont);
      lv_obj_set_size(ghdr, lv_pct(100), 20);
      lv_obj_set_style_bg_color(ghdr, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
      lv_obj_set_style_border_width(ghdr, 0, 0);
      lv_obj_set_style_pad_all(ghdr, 2, 0);
      lv_obj_set_style_radius(ghdr, 3, 0);

      lv_obj_t *glbl = lv_label_create(ghdr);
      char hdr_txt[EOS_STR_48_LEN];
      snprintf(hdr_txt, sizeof(hdr_txt), "== %s ==", gname);
      lv_label_set_text(glbl, hdr_txt);
      lv_obj_set_style_text_color(glbl, lv_color_hex(EOS_THEME_INFO_BLUE), 0);
      lv_obj_align(glbl, LV_ALIGN_LEFT_MID, 4, 0);
    }

    int idx = st->filtered_count++;
    st->filtered[idx] = b;

    lv_obj_t *row = lv_obj_create(st->list_cont);
    lv_obj_set_size(row, lv_pct(100), 28);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, launcher_item_event_cb, LV_EVENT_CLICKED, st);

    lv_obj_t *lbl_name = lv_label_create(row);
    lv_label_set_text(lbl_name, bin_effective_name(b));
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(lbl_name, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl_tag = lv_label_create(row);
    char tag_buf[EOS_SMALL_STR_LEN];
    if (target_grp) {
      snprintf(tag_buf, sizeof(tag_buf), "%s", b->filename);
    } else {
      snprintf(tag_buf, sizeof(tag_buf), "[%s]", gname);
    }
    lv_label_set_text(lbl_tag, tag_buf);
    lv_obj_set_style_text_color(lbl_tag, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
    lv_obj_align(lbl_tag, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(lbl_tag, LV_OBJ_FLAG_CLICKABLE);

    st->item_btns[idx] = row;
  }

  if (st->selected_item >= st->filtered_count) {
    st->selected_item = (st->filtered_count > 0) ? (st->filtered_count - 1) : 0;
  }
  if (st->selected_item < 0) {
    st->selected_item = 0;
  }

  launcher_update_details(st);
}

static void launcher_build_ui(launcher_state_t *st) {
  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  bool compact_mode = (h <= EOS_LVGL_DIALOG_MIN_W);

  // Top navigation / group selector bar
  int32_t top_y = 0;
  if (!compact_mode) {
    // 1. Header bar for large displays
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, (int32_t)w, 24);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1E3A8A), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 6, 0);
    lv_obj_set_style_pad_ver(hdr, 2, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    st->hdr_title = lv_label_create(hdr);
    lv_label_set_text(st->hdr_title, "EOS Launcher");
    lv_obj_set_style_text_color(st->hdr_title, lv_color_hex(EOS_THEME_TEXT_WHITE), 0);
    lv_obj_align(st->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);
    top_y = 24;
  }

  // 2. Group selector bar (height 22)
  st->grp_bar = lv_obj_create(scr);
  lv_obj_set_size(st->grp_bar, (int32_t)w, 22);
  lv_obj_align(st->grp_bar, LV_ALIGN_TOP_MID, 0, top_y);
  lv_obj_set_style_bg_color(st->grp_bar, lv_color_hex(EOS_COLOR_BG_SUBHDR), 0);
  lv_obj_set_style_border_width(st->grp_bar, 0, 0);
  lv_obj_set_style_radius(st->grp_bar, 0, 0);
  lv_obj_set_style_pad_all(st->grp_bar, 2, 0);
  lv_obj_clear_flag(st->grp_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(st->grp_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(st->grp_bar, launcher_grp_event_cb, LV_EVENT_CLICKED, st);

  st->grp_label = lv_label_create(st->grp_bar);
  lv_obj_set_style_text_color(st->grp_label, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
  lv_obj_center(st->grp_label);
  lv_obj_clear_flag(st->grp_label, LV_OBJ_FLAG_CLICKABLE);

  if (compact_mode) {
    st->hdr_title = NULL;
  }

  // 3. App list container
  int32_t list_y = top_y + 24;
  int32_t footer_h = compact_mode ? 0 : 38;
  int32_t list_h = (int32_t)h - list_y - footer_h - (compact_mode ? 2 : 4);
  if (list_h < LAUNCHER_MIN_LIST_H)
    list_h = LAUNCHER_MIN_LIST_H;

  st->list_cont = lv_obj_create(scr);
  lv_obj_set_size(st->list_cont, (int32_t)w - 6, list_h);
  lv_obj_align(st->list_cont, LV_ALIGN_TOP_MID, 0, list_y);
  lv_obj_set_style_bg_color(st->list_cont, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(st->list_cont, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(st->list_cont, 1, 0);
  lv_obj_set_style_radius(st->list_cont, 4, 0);
  lv_obj_set_style_pad_all(st->list_cont, 3, 0);
  lv_obj_set_style_pad_row(st->list_cont, 2, 0);
  lv_obj_set_flex_flow(st->list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(st->list_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_flag(st->list_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_add_flag(st->list_cont, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(scr, launcher_gesture_cb, LV_EVENT_GESTURE, st);
  lv_obj_add_event_cb(st->list_cont, launcher_gesture_cb, LV_EVENT_GESTURE, st);

  if (!compact_mode) {
    // 4. Info card footer on large displays
    lv_obj_t *footer = lv_obj_create(scr);
    lv_obj_set_size(footer, (int32_t)w - 6, footer_h);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_color(footer, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(0x475569), 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_radius(footer, 4, 0);
    lv_obj_set_style_pad_all(footer, 2, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    st->info_title = lv_label_create(footer);
    lv_obj_set_width(st->info_title, (int32_t)w - 16);
    lv_label_set_long_mode(st->info_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(st->info_title, lv_color_hex(EOS_THEME_INFO_BLUE), 0);
    lv_obj_align(st->info_title, LV_ALIGN_TOP_LEFT, 2, 0);

    st->info_desc = lv_label_create(footer);
    lv_obj_set_width(st->info_desc, (int32_t)w - 16);
    lv_label_set_long_mode(st->info_desc, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(st->info_desc, lv_color_hex(0xE2E8F0), 0);
    lv_obj_align(st->info_desc, LV_ALIGN_BOTTOM_LEFT, 2, 0);
  } else {
    st->info_title = NULL;
    st->info_desc = NULL;
  }

  launcher_populate_list(st);
}

static void launcher_handle_key(launcher_state_t *st, uint16_t key) {
  switch (key) {
  case KEY_UP:
    if (st->filtered_count > 0) {
      st->selected_item =
          (st->selected_item - 1 + st->filtered_count) % st->filtered_count;
      launcher_update_details(st);
    }
    break;

  case KEY_DOWN:
    if (st->filtered_count > 0) {
      st->selected_item = (st->selected_item + 1) % st->filtered_count;
      launcher_update_details(st);
    }
    break;

  case KEY_LEFT:
    if (st->group_count > 1) {
      st->selected_group =
          (st->selected_group - 1 + st->group_count) % st->group_count;
      st->selected_item = 0;
      launcher_populate_list(st);
    }
    break;

  case KEY_RIGHT:
    if (st->group_count > 1) {
      st->selected_group = (st->selected_group + 1) % st->group_count;
      st->selected_item = 0;
      launcher_populate_list(st);
    }
    break;

  case KEY_D:
    launcher_toggle_desc(st);
    break;

  case KEY_ENTER:
  case KEY_A:
  case KEY_SPACE:
    if (st->desc_dialog) {
      launcher_toggle_desc(st);
      break;
    }
    if (st->filtered_count > 0 && st->selected_item >= 0 &&
        st->selected_item < st->filtered_count) {
      st->trigger_launch = true;
    }
    break;

  case KEY_ESC:
  case KEY_B:
  case KEY_BACKSPACE:
    if (st->desc_dialog) {
      launcher_toggle_desc(st);
    }
    break;

  default:
    break;
  }
}

static int launcher_run_selected(launcher_state_t *st, const char *fb_path,
                                 const char *kbd_path) {
  if (st->filtered_count <= 0 || st->selected_item < 0 ||
      st->selected_item >= st->filtered_count) {
    return -1;
  }

  const eos_bin_t *target = st->filtered[st->selected_item];
  if (!target)
    return -1;

  bool is_direct = bin_is_direct_display_or_shell(target);

  eos_toolbar_printf(NULL, "Launching %s...", target->filename);

  if (st->desc_dialog) {
    lv_obj_delete(st->desc_dialog);
    st->desc_dialog = NULL;
  }

  // Release framebuffer, keyboard, and LVGL buffers before spawning child binary
  eos_lvgl_deinit();

  // Snapshot active file descriptors in launcher thread before running child app
  eos_proc_fd_snapshot_t app_snap = eos_proc_snapshot_fds();

  struct timeval t0, t1;
  gettimeofday(&t0, NULL);

  printf("\n[launcher] Starting '%s' (%s)...\n", target->filename,
         bin_effective_name(target));

  int rc = 0;
  if (is_direct) {
    if (st->custom_cmd[0] != '\0') {
      rc = system(st->custom_cmd);
      st->custom_cmd[0] = '\0';
    } else {
      rc = system(target->filename);
    }
    if (rc < 0 && target->entry_point) {
      char arg0[EOS_SMALL_STR_LEN];
      snprintf(arg0, sizeof(arg0), "%s", target->filename);
      char *argv_local[2] = {arg0, NULL};
      rc = target->entry_point(1, argv_local);
    }

    // Clean up any file descriptors leaked by the direct application
    eos_proc_cleanup_leaked_fds(&app_snap);

    gettimeofday(&t1, NULL);
    int64_t elapsed_ms = (int64_t)(t1.tv_sec - t0.tv_sec) * (int64_t)EOS_MS_PER_SEC +
                         (int64_t)(t1.tv_usec - t0.tv_usec) / (int64_t)EOS_US_PER_MS;

    // If the app drew a static frame and exited immediately (e.g. test_disp_bars),
    // pause briefly so its output stays visible before restoring the launcher UI.
    if (elapsed_ms >= 0 && elapsed_ms < 250) {
      usleep(600000);
    }
  } else {
    // Terminal/CLI task: pipe stdout & stderr into LAUNCHER_TERM_OUT_FILE
    // and display the results in lvgl_term.
    int in_fd = open(LAUNCHER_TERM_IN_FILE, O_WRONLY | O_CREAT | O_TRUNC, EOS_DEFAULT_FILE_MODE);
    if (in_fd >= 0) {
      close(in_fd);
    }

    char cmd_str[EOS_MID_STR_LEN];
    if (strcmp(target->filename, "test_input") == 0) {
      snprintf(cmd_str, sizeof(cmd_str), "test_input 8");
    } else {
      snprintf(cmd_str, sizeof(cmd_str), "%s", target->filename);
    }

    char redir_cmd[EOS_LARGE_STR_LEN];
    snprintf(redir_cmd, sizeof(redir_cmd), "%s < %s > %s 2>&1", cmd_str,
         LAUNCHER_TERM_IN_FILE, LAUNCHER_TERM_OUT_FILE);
    rc = system(redir_cmd);

    // Clean up any file descriptors leaked by the child app before launching lvgl_term
    eos_proc_cleanup_leaked_fds(&app_snap);

    eos_proc_fd_snapshot_t term_snap = eos_proc_snapshot_fds();
    char view_cmd[EOS_LARGE_STR_LEN];
    snprintf(view_cmd, sizeof(view_cmd),
             "lvgl_term %s \"%s (exit %d)\" \"%s\"", LAUNCHER_TERM_OUT_FILE,
             target->filename, rc, cmd_str);
    (void)system(view_cmd);
    eos_proc_cleanup_leaked_fds(&term_snap);
  }

  // Safety cleanup: ensure any remaining leaked descriptors are closed before reinitializing LVGL
  eos_proc_cleanup_leaked_fds(&app_snap);

  st->last_exit_code = rc;
  snprintf(st->last_launched, sizeof(st->last_launched), "%s",
           target->filename);
  st->has_launched = true;

  // Re-initialize LVGL and rebuild launcher screen
  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    EOS_LOGE("Failed to reinitialize LVGL after running '%s'",
             target->filename);
    return -1;
  }

  eos_lvgl_clear_keys();
  launcher_build_ui(st);

  eos_toolbar_set("EOS Launcher", "[▲▼]Select  [◄►]Tab  [A]Launch");

  EOS_LOGI("launcher: stack watermark=%u bytes free (allocated %u, used ~%u bytes)",
           (unsigned)eos_thread_stack_high_water_mark(),
           (unsigned)EOS_BIN_GUI_STACK_SIZE,
           (unsigned)(EOS_BIN_GUI_STACK_SIZE - eos_thread_stack_high_water_mark()));
  return 0;
}

static int launcher_check_active(int argc, char **argv, bool list_only,
                                 const char *app_to_launch) {
  bool launcher_is_active = false;
  FILE *act_check = fopen(LAUNCHER_ACTIVE_FILE, "r");
  if (act_check) {
    long act_pid = 0;
    if (fscanf(act_check, "%ld", &act_pid) == 1 && act_pid > 0) {
      char ppath[EOS_SMALL_STR_LEN];
      snprintf(ppath, sizeof(ppath), "/proc/%ld", act_pid);
      if (access(ppath, F_OK) == 0) {
        launcher_is_active = true;
      }
    }
    fclose(act_check);
    if (!launcher_is_active) {
      unlink(LAUNCHER_ACTIVE_FILE);
    }
  }

  if (!list_only && launcher_is_active) {
    if (app_to_launch) {
      FILE *fp = fopen(LAUNCHER_CMD_FILE, "w");
      if (fp) {
        for (int i = 1; i < argc; i++) {
          if (i > 1) fputc(' ', fp);
          fputs(argv[i], fp);
        }
        fputc('\n', fp);
        fclose(fp);
      }
      printf("launcher: requesting launch of '%s'...\n", app_to_launch);
      return 1;
    }
    EOS_LOGW("Launcher is already running");
    return 1;
  }
  return 0;
}

static void launcher_handle_incoming_cmd(launcher_state_t *st) {
  if (access(LAUNCHER_CMD_FILE, F_OK) != 0) {
    return;
  }

  char target_name[EOS_SMALL_STR_LEN] = {0};
  FILE *fp = fopen(LAUNCHER_CMD_FILE, "r");
  if (fp) {
    if (fgets(target_name, sizeof(target_name), fp)) {
      size_t l = strlen(target_name);
      while (l > 0 && (target_name[l - 1] == '\r' || target_name[l - 1] == '\n')) {
        target_name[--l] = '\0';
      }
    }
    fclose(fp);
  }
  unlink(LAUNCHER_CMD_FILE);

  if (target_name[0] == '\0') {
    return;
  }

  char bin_name[EOS_SMALL_STR_LEN];
  strlcpy(bin_name, target_name, sizeof(bin_name));
  char *sp = strchr(bin_name, ' ');
  if (sp) *sp = '\0';
  strlcpy(st->custom_cmd, target_name, sizeof(st->custom_cmd));

  for (int i = 0; i < st->bin_count; i++) {
    if (strcmp(st->bins[i]->filename, bin_name) == 0) {
      const char *grp = bin_effective_group(st->bins[i]);
      for (int g = 0; g < st->group_count; g++) {
        if (strcmp(st->groups[g].name, grp) == 0) {
          st->selected_group = g;
          launcher_populate_list(st);
          break;
        }
      }
      for (int j = 0; j < st->filtered_count; j++) {
        if (strcmp(st->filtered[j]->filename, bin_name) == 0) {
          st->selected_item = j;
          launcher_update_details(st);
          st->trigger_launch = true;
          break;
        }
      }
      break;
    }
  }
}

static void launcher_run_loop(launcher_state_t *st, const char *fb_path,
                              const char *kbd_path) {
  while (!st->should_exit) {
    launcher_handle_incoming_cmd(st);

    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      launcher_handle_key(st, key);
      if (st->should_exit || st->trigger_launch)
        break;
    }

    if (st->trigger_launch) {
      st->trigger_launch = false;
      if (launcher_run_selected(st, fb_path, kbd_path) != 0) {
        break;
      }
      continue;
    }

    eos_lvgl_step(10);
  }
}

int launcher_main(int argc, char **argv) {
  bool list_only = false;
  bool include_hidden = false;
  const char *app_to_launch = NULL;
  const char *fb_path = NULL;
  const char *kbd_path = NULL;

  if (argc > 1) {
    if (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0) {
      list_only = true;
    } else if (strcmp(argv[1], "-a") == 0 || strcmp(argv[1], "--all") == 0) {
      list_only = true;
      include_hidden = true;
    } else if (argv[1][0] == '/') {
      fb_path = argv[1];
      if (argc > 2 && argv[2][0] == '/') {
        kbd_path = argv[2];
      }
    } else {
      app_to_launch = argv[1];
    }
  }

  if (launcher_check_active(argc, argv, list_only, app_to_launch)) {
    return 0;
  }

  launcher_state_t *st = calloc(1, sizeof(launcher_state_t));
  if (!st) {
    perror("launcher calloc");
    return -1;
  }

  launcher_discover_bins(st, include_hidden);

  if (list_only) {
    launcher_print_grouped_list(st);
    free(st);
    return 0;
  }

  FILE *act = fopen(LAUNCHER_ACTIVE_FILE, "w");
  if (act) {
    fprintf(act, "%ld\n", (long)getpid());
    fclose(act);
  }

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("launcher: LVGL init failed, printing ebin list instead:\n");
    launcher_print_grouped_list(st);
    free(st);
    unlink(LAUNCHER_ACTIVE_FILE);
    return -1;
  }

  printf("launcher: started on %s (%ux%u, %s)\n",
         eos_lvgl_get_fb_path(),
         (unsigned)eos_lvgl_get_width(), (unsigned)eos_lvgl_get_height(),
         eos_lvgl_get_indev() ? "keyboard active" : "touch/display mode (no keyboard)");

  launcher_build_ui(st);

  eos_toolbar_set("EOS Launcher", "[▲▼]Select  [◄►]Tab  [A]Launch");

  EOS_LOGI("launcher: stack watermark=%u bytes free (allocated %u, used ~%u bytes)",
           (unsigned)eos_thread_stack_high_water_mark(),
           (unsigned)EOS_BIN_GUI_STACK_SIZE,
           (unsigned)(EOS_BIN_GUI_STACK_SIZE - eos_thread_stack_high_water_mark()));

  if (app_to_launch) {
    for (int i = 0; i < st->filtered_count; i++) {
      if (strcmp(st->filtered[i]->filename, app_to_launch) == 0) {
        st->selected_item = i;
        launcher_update_details(st);
        st->trigger_launch = true;
        break;
      }
    }
  }

  launcher_run_loop(st, fb_path, kbd_path);

  eos_lvgl_deinit();
  free(st);
  unlink(LAUNCHER_ACTIVE_FILE);
  unlink(LAUNCHER_CMD_FILE);
  return 0;
}

EOS_BIN_ATTR const eos_bin_t launcher_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN | EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "launcher",
    .name = "App Launcher",
    .group = "System",
    .description = "Graphical application launcher with category tabs and command execution",
    .entry_point = launcher_main,
};
