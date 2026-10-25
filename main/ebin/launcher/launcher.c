#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#define LAUNCHER_MAX_BINS      96
#define LAUNCHER_MAX_GROUPS    24
#define LAUNCHER_DEFAULT_GROUP "System"
#define LAUNCHER_TERM_OUT_FILE "/term.out"
#define LAUNCHER_TERM_IN_FILE  "/term.in"

static volatile bool s_launcher_active = false;

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
  bool has_launched;
  bool should_exit;
  bool trigger_launch;

  // LVGL widgets
  lv_obj_t *hdr_title;
  lv_obj_t *hdr_mode;
  lv_obj_t *grp_bar;
  lv_obj_t *grp_label;
  lv_obj_t *list_cont;
  lv_obj_t *item_btns[LAUNCHER_MAX_BINS];
  lv_obj_t *info_title;
  lv_obj_t *info_desc;
  lv_obj_t *status_bar;
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

static void launcher_update_details(launcher_state_t *st) {
  if (!st->hdr_mode)
    return;

  lv_label_set_text(st->hdr_mode,
                    eos_lvgl_is_full_fallback() ? "FULL" : "PARTIAL");

  if (st->grp_label && st->group_count > 0) {
    const launcher_group_t *grp = &st->groups[st->selected_group];
    char gbuf[64];
    snprintf(gbuf, sizeof(gbuf), "<  %s (%d)  >   [%d/%d]", grp->name,
             grp->count, st->selected_group + 1, st->group_count);
    lv_label_set_text(st->grp_label, gbuf);
  }

  for (int i = 0; i < st->filtered_count; i++) {
    lv_obj_t *btn = st->item_btns[i];
    if (!btn)
      continue;
    if (i == st->selected_item) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x2563EB), 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0x93C5FD), 0);
      lv_obj_set_style_border_width(btn, 2, 0);
      lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
    } else {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0x334155), 0);
      lv_obj_set_style_border_width(btn, 1, 0);
    }
  }

  if (st->filtered_count > 0 && st->selected_item >= 0 &&
      st->selected_item < st->filtered_count) {
    const eos_bin_t *b = st->filtered[st->selected_item];
    char tbuf[96];
    snprintf(tbuf, sizeof(tbuf), "%s (/bin/%s) [%s]", bin_effective_name(b),
             b->filename, bin_effective_group(b));
    lv_label_set_text(st->info_title, tbuf);
    lv_label_set_text(st->info_desc, bin_effective_desc(b));
  } else {
    lv_label_set_text(st->info_title, "No applications in group");
    lv_label_set_text(st->info_desc, "Use LEFT/RIGHT to switch group");
  }

  if (st->status_bar) {
    char sbuf[96];
    if (st->has_launched) {
      snprintf(sbuf, sizeof(sbuf), "Last: %s (exit %d) | A:Run B:Exit",
               st->last_launched, st->last_exit_code);
    } else {
      snprintf(sbuf, sizeof(sbuf), "U/D:App L/R:Grp A/ENT:Run C:Mode B:Quit");
    }
    lv_label_set_text(st->status_bar, sbuf);
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
      lv_obj_set_style_bg_color(ghdr, lv_color_hex(0x0F172A), 0);
      lv_obj_set_style_border_width(ghdr, 0, 0);
      lv_obj_set_style_pad_all(ghdr, 2, 0);
      lv_obj_set_style_radius(ghdr, 3, 0);

      lv_obj_t *glbl = lv_label_create(ghdr);
      char hdr_txt[48];
      snprintf(hdr_txt, sizeof(hdr_txt), "== %s ==", gname);
      lv_label_set_text(glbl, hdr_txt);
      lv_obj_set_style_text_color(glbl, lv_color_hex(0x38BDF8), 0);
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

    lv_obj_t *lbl_name = lv_label_create(row);
    lv_label_set_text(lbl_name, bin_effective_name(b));
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *lbl_tag = lv_label_create(row);
    char tag_buf[32];
    if (target_grp) {
      snprintf(tag_buf, sizeof(tag_buf), "%s", b->filename);
    } else {
      snprintf(tag_buf, sizeof(tag_buf), "[%s]", gname);
    }
    lv_label_set_text(lbl_tag, tag_buf);
    lv_obj_set_style_text_color(lbl_tag, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(lbl_tag, LV_ALIGN_RIGHT_MID, 0, 0);

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
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1120), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Header bar (height 24)
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
  lv_obj_set_style_text_color(st->hdr_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(st->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);

  st->hdr_mode = lv_label_create(hdr);
  lv_label_set_text(st->hdr_mode, "PARTIAL");
  lv_obj_set_style_text_color(st->hdr_mode, lv_color_hex(0x34D399), 0);
  lv_obj_align(st->hdr_mode, LV_ALIGN_RIGHT_MID, 0, 0);

  // 2. Group selector bar (height 24, y=24)
  st->grp_bar = lv_obj_create(scr);
  lv_obj_set_size(st->grp_bar, (int32_t)w, 24);
  lv_obj_align(st->grp_bar, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_set_style_bg_color(st->grp_bar, lv_color_hex(0x172554), 0);
  lv_obj_set_style_border_width(st->grp_bar, 0, 0);
  lv_obj_set_style_radius(st->grp_bar, 0, 0);
  lv_obj_set_style_pad_all(st->grp_bar, 2, 0);
  lv_obj_clear_flag(st->grp_bar, LV_OBJ_FLAG_SCROLLABLE);

  st->grp_label = lv_label_create(st->grp_bar);
  lv_obj_set_style_text_color(st->grp_label, lv_color_hex(0xFDE047), 0);
  lv_obj_center(st->grp_label);

  // 3. App list container (y=50, height = h - 50 - 64)
  int32_t list_h = (int32_t)h - 114;
  if (list_h < 60)
    list_h = 60;

  st->list_cont = lv_obj_create(scr);
  lv_obj_set_size(st->list_cont, (int32_t)w - 6, list_h);
  lv_obj_align(st->list_cont, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_color(st->list_cont, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_border_color(st->list_cont, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(st->list_cont, 1, 0);
  lv_obj_set_style_radius(st->list_cont, 4, 0);
  lv_obj_set_style_pad_all(st->list_cont, 4, 0);
  lv_obj_set_style_pad_row(st->list_cont, 3, 0);
  lv_obj_set_flex_flow(st->list_cont, LV_FLEX_FLOW_COLUMN);

  // 4. Info & status footer (bottom 62px)
  lv_obj_t *footer = lv_obj_create(scr);
  lv_obj_set_size(footer, (int32_t)w - 6, 60);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_set_style_bg_color(footer, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_border_color(footer, lv_color_hex(0x475569), 0);
  lv_obj_set_style_border_width(footer, 1, 0);
  lv_obj_set_style_radius(footer, 4, 0);
  lv_obj_set_style_pad_all(footer, 4, 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  st->info_title = lv_label_create(footer);
  lv_obj_set_width(st->info_title, (int32_t)w - 18);
  lv_label_set_long_mode(st->info_title, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->info_title, lv_color_hex(0x38BDF8), 0);
  lv_obj_align(st->info_title, LV_ALIGN_TOP_LEFT, 0, 0);

  st->info_desc = lv_label_create(footer);
  lv_obj_set_width(st->info_desc, (int32_t)w - 18);
  lv_label_set_long_mode(st->info_desc, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->info_desc, lv_color_hex(0xE2E8F0), 0);
  lv_obj_align(st->info_desc, LV_ALIGN_LEFT_MID, 0, 1);

  st->status_bar = lv_label_create(footer);
  lv_obj_set_width(st->status_bar, (int32_t)w - 18);
  lv_label_set_long_mode(st->status_bar, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(st->status_bar, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(st->status_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);

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
  case KEY_D:
    if (st->group_count > 1) {
      st->selected_group = (st->selected_group + 1) % st->group_count;
      st->selected_item = 0;
      launcher_populate_list(st);
    }
    break;

  case KEY_C:
    eos_lvgl_set_full_fallback(!eos_lvgl_is_full_fallback());
    launcher_update_details(st);
    break;

  case KEY_ENTER:
  case KEY_A:
  case KEY_SPACE:
    if (st->filtered_count > 0 && st->selected_item >= 0 &&
        st->selected_item < st->filtered_count) {
      st->trigger_launch = true;
    }
    break;

  case KEY_ESC:
  case KEY_B:
    st->should_exit = true;
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

  if (st->status_bar) {
    char msg[64];
    if (is_direct) {
      snprintf(msg, sizeof(msg), "Launching %s...", target->filename);
    } else {
      snprintf(msg, sizeof(msg), "Running %s -> %s...", target->filename,
               LAUNCHER_TERM_OUT_FILE);
    }
    lv_label_set_text(st->status_bar, msg);
    if (eos_lvgl_get_display()) {
      lv_refr_now(eos_lvgl_get_display());
    }
  }

  // Release framebuffer, keyboard, and LVGL buffers before spawning child binary
  eos_lvgl_deinit();

  // Snapshot active file descriptors in launcher thread before running child app
  eos_tctx_t *tctx = eos_tctx_get();
  eos_tctx_fd_snapshot_t app_snap = eos_tctx_snapshot_fds(tctx);

  struct timeval t0, t1;
  gettimeofday(&t0, NULL);

  printf("\n[launcher] Starting '%s' (%s)...\n", target->filename,
         bin_effective_name(target));

  int rc = 0;
  if (is_direct) {
    rc = system(target->filename);
    if (rc < 0 && target->entry_point) {
      char arg0[EOS_SMALL_STR_LEN];
      snprintf(arg0, sizeof(arg0), "%s", target->filename);
      char *argv_local[2] = {arg0, NULL};
      rc = target->entry_point(1, argv_local);
    }

    // Clean up any file descriptors leaked by the direct application
    eos_tctx_cleanup_leaked_fds(tctx, &app_snap);

    gettimeofday(&t1, NULL);
    int64_t elapsed_ms = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000LL +
                         (int64_t)(t1.tv_usec - t0.tv_usec) / 1000LL;

    // If the app drew a static frame and exited immediately (e.g. test_disp_bars),
    // pause briefly so its output stays visible before restoring the launcher UI.
    if (elapsed_ms >= 0 && elapsed_ms < 250) {
      usleep(600000);
    }
  } else {
    // Terminal/CLI task: pipe stdout & stderr into LAUNCHER_TERM_OUT_FILE
    // and display the results in lvgl_term.
    int in_fd = open(LAUNCHER_TERM_IN_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
    eos_tctx_cleanup_leaked_fds(tctx, &app_snap);

    eos_tctx_fd_snapshot_t term_snap = eos_tctx_snapshot_fds(tctx);
    char view_cmd[EOS_LARGE_STR_LEN];
    snprintf(view_cmd, sizeof(view_cmd),
             "lvgl_term %s \"%s (exit %d)\" \"%s\"", LAUNCHER_TERM_OUT_FILE,
             target->filename, rc, cmd_str);
    (void)system(view_cmd);
    eos_tctx_cleanup_leaked_fds(tctx, &term_snap);
  }

  // Safety cleanup: ensure any remaining leaked descriptors are closed before reinitializing LVGL
  eos_tctx_cleanup_leaked_fds(tctx, &app_snap);

  st->last_exit_code = rc;
  snprintf(st->last_launched, sizeof(st->last_launched), "%s",
           target->filename);
  st->has_launched = true;

  // Re-initialize LVGL and rebuild launcher screen
  eos_lvgl_cfg_t cfg = {
      .fb_path = fb_path,
      .kbd_path = kbd_path,
      .partial_lines = 0,
      .force_full_frame = false,
      .dpad_group_nav = false,
      .enable_keyboard = true,
  };
  if (eos_lvgl_init(&cfg) != 0) {
    EOS_LOGE("Failed to reinitialize LVGL after running '%s'",
             target->filename);
    return -1;
  }

  eos_lvgl_clear_keys();
  launcher_build_ui(st);
  return 0;
}

int launcher_main(int argc, char **argv) {
  bool list_only = false;
  bool include_hidden = false;

  if (argc > 1) {
    if (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0) {
      list_only = true;
    } else if (strcmp(argv[1], "-a") == 0 || strcmp(argv[1], "--all") == 0) {
      list_only = true;
      include_hidden = true;
    }
  }

  if (!list_only && s_launcher_active) {
    EOS_LOGW("Launcher is already running");
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

  s_launcher_active = true;

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
    printf("launcher: LVGL init failed, printing ebin list instead:\n");
    launcher_print_grouped_list(st);
    free(st);
    s_launcher_active = false;
    return -1;
  }

  launcher_build_ui(st);

  while (!st->should_exit) {
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

  eos_lvgl_deinit();
  free(st);
  s_launcher_active = false;
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
