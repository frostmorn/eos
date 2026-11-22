#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "ebin/lvgl_snake/lvgl_snake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
  snake_logic_t logic;
  int cell_size;
  int board_w;
  int board_h;
  bool quit;

  lv_obj_t *board;
  lv_obj_t *seg_objs[SNAKE_MAX_LEN];
  lv_obj_t *food_obj;
  lv_obj_t *score_lbl;
  lv_obj_t *status_lbl;
} snake_game_t;

static uint64_t now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * (uint64_t)EOS_MS_PER_SEC + (uint64_t)tv.tv_usec / (uint64_t)EOS_US_PER_MS;
}

static void snake_sync_ui(snake_game_t *g) {
  int cs = g->cell_size > 0 ? g->cell_size : 10;
  if (g->food_obj) {
    lv_obj_set_pos(g->food_obj, g->logic.food.x * cs + 1,
                   g->logic.food.y * cs + 1);
  }

  for (int i = 0; i < SNAKE_MAX_LEN; i++) {
    if (i < g->logic.len) {
      if (!g->seg_objs[i]) {
        lv_obj_t *seg = lv_obj_create(g->board);
        lv_obj_set_size(seg, cs - 2, cs - 2);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_radius(seg, 2, 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        g->seg_objs[i] = seg;
      }
      lv_obj_clear_flag(g->seg_objs[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(g->seg_objs[i], g->logic.body[i].x * cs + 1,
                     g->logic.body[i].y * cs + 1);
      lv_obj_set_style_bg_color(
          g->seg_objs[i],
          (i == 0) ? lv_color_hex(EOS_THEME_ACCENT_GREEN) : lv_color_hex(0x10B981), 0);
    } else if (g->seg_objs[i]) {
      lv_obj_add_flag(g->seg_objs[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  char sbuf[EOS_LINE_STR_LEN];
  snprintf(sbuf, sizeof(sbuf), "Score: %d   Best: %d   [%s]", g->logic.score,
           g->logic.high_score, g->logic.fast_mode ? "FAST" : "NORM");
  lv_label_set_text(g->score_lbl, sbuf);

  if (g->logic.game_over) {
    lv_label_set_text(g->status_lbl, "GAME OVER! Press A/ENTER to retry, B:Exit");
  } else if (g->logic.paused) {
    lv_label_set_text(g->status_lbl, "PAUSED — Press A/ENTER to resume, B:Exit");
  } else {
    lv_label_set_text(g->status_lbl, "Arrows:Steer A:Pause C:Speed B/ESC:Quit");
  }

  FILE *tb = fopen("/tmp/tbar.tmp", "w");
  if (tb) {
    if (g->logic.game_over) {
      fprintf(tb, "@%ld Game Over (Score: %d)\n[A]Retry  [B]Exit\n", (long)getpid(), g->logic.score);
    } else if (g->logic.paused) {
      fprintf(tb, "@%ld Game Paused\n[A]Resume  [B]Exit\n", (long)getpid());
    } else {
      fprintf(tb, "@%ld Snake Game\n[▲▼◄►]Steer  [A]Pause  [C]Speed  [B]Exit\n", (long)getpid());
    }
    fclose(tb);
    rename("/tmp/tbar.tmp", "/tmp/tbar");
  }
}

static void snake_tick(snake_game_t *g) {
  if (snake_logic_tick(&g->logic)) {
    snake_sync_ui(g);
  }
}

static void snake_back_btn_cb(lv_event_t *e) {
  snake_game_t *g = (snake_game_t *)lv_event_get_user_data(e);
  if (g) {
    g->quit = true;
  }
}

static void snake_build_ui(snake_game_t *g) {
  uint32_t scr_w = eos_lvgl_get_width();
  uint32_t scr_h = eos_lvgl_get_height();

  int header_h = 18;
  int footer_h = 16;
  int avail_w = (int)scr_w - 12;
  int avail_h = (int)scr_h - header_h - footer_h - 6;

  int cols = 18;
  int rows = 14;
  int cell_size = avail_h / rows;
  if (cell_size * cols > avail_w) {
    cell_size = avail_w / cols;
  }
  if (cell_size < 6) cell_size = 6;
  if (cell_size > 22) cell_size = 22;

  snake_logic_init(&g->logic, cols, rows);
  g->cell_size = cell_size;
  g->board_w = cols * cell_size + 4;
  g->board_h = rows * cell_size + 4;

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  g->score_lbl = lv_label_create(scr);
  lv_obj_set_style_text_color(g->score_lbl, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
  lv_obj_align(g->score_lbl, LV_ALIGN_TOP_MID, 0, 4);

  g->board = lv_obj_create(scr);
  lv_obj_set_size(g->board, g->board_w, g->board_h);
  lv_obj_align(g->board, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(g->board, lv_color_hex(0x111827), 0);
  lv_obj_set_style_border_color(g->board, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_border_width(g->board, 2, 0);
  lv_obj_set_style_radius(g->board, 4, 0);
  lv_obj_set_style_pad_all(g->board, 0, 0);
  lv_obj_clear_flag(g->board, LV_OBJ_FLAG_SCROLLABLE);

  g->food_obj = lv_obj_create(g->board);
  lv_obj_set_size(g->food_obj, g->cell_size - 2, g->cell_size - 2);
  lv_obj_set_style_bg_color(g->food_obj, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_border_width(g->food_obj, 0, 0);
  lv_obj_set_style_radius(g->food_obj, g->cell_size / 2, 0);
  lv_obj_clear_flag(g->food_obj, LV_OBJ_FLAG_SCROLLABLE);

  g->status_lbl = lv_label_create(scr);
  lv_obj_set_style_text_color(g->status_lbl, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
  lv_obj_align(g->status_lbl, LV_ALIGN_BOTTOM_MID, 0, -4);

  lv_obj_t *back_btn = eos_lvgl_create_back_btn(scr, snake_back_btn_cb, g);
  if (back_btn) {
    lv_obj_set_size(back_btn, 24, 18);
    lv_obj_align(back_btn, LV_ALIGN_TOP_RIGHT, -4, 2);
  }
}

static void snake_handle_key(snake_game_t *g, uint16_t key) {
  switch (key) {
  case KEY_UP:
    snake_logic_set_direction(&g->logic, 0, -1);
    break;
  case KEY_DOWN:
    snake_logic_set_direction(&g->logic, 0, 1);
    break;
  case KEY_LEFT:
    snake_logic_set_direction(&g->logic, -1, 0);
    break;
  case KEY_RIGHT:
    snake_logic_set_direction(&g->logic, 1, 0);
    break;
  case KEY_A:
  case KEY_ENTER:
  case KEY_SPACE:
    snake_logic_toggle_pause(&g->logic);
    snake_sync_ui(g);
    break;
  case KEY_C:
    snake_logic_toggle_speed(&g->logic);
    snake_sync_ui(g);
    break;
  case KEY_B:
  case KEY_BACKSPACE:
  case KEY_ESC:
    g->quit = true;
    break;
  default:
    break;
  }
}

int lvgl_snake_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_snake: failed to init LVGL\n");
    return -1;
  }

  snake_game_t *g = calloc(1, sizeof(snake_game_t));
  if (!g) {
    eos_lvgl_deinit();
    return -1;
  }

  srand((unsigned)now_ms());
  snake_build_ui(g);
  snake_sync_ui(g);

  uint64_t last_step = now_ms();

  while (!g->quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      snake_handle_key(g, key);
    }

    uint64_t cur = now_ms();
    uint64_t interval = g->logic.fast_mode ? 95ULL : 160ULL;
    if (cur - last_step >= interval) {
      last_step = cur;
      snake_tick(g);
    }

    eos_lvgl_step(10);
  }

  eos_lvgl_deinit();
  free(g);
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_snake_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_snake",
    .name = "Snake",
    .group = "Games",
    .description = "Classic arcade Snake mini-game with score tracking and speed levels",
    .entry_point = lvgl_snake_main,
};
