#include "ecore/bin.h"
#include "ecore/lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define SNAKE_COLS      18
#define SNAKE_ROWS      16
#define SNAKE_CELL_SIZE 12
#define SNAKE_MAX_LEN   (SNAKE_COLS * SNAKE_ROWS)

typedef struct {
  int8_t x;
  int8_t y;
} snake_pt_t;

typedef struct {
  snake_pt_t body[SNAKE_MAX_LEN];
  int len;
  int8_t dir_x;
  int8_t dir_y;
  int8_t next_dx;
  int8_t next_dy;
  snake_pt_t food;
  int score;
  int high_score;
  bool paused;
  bool game_over;
  bool fast_mode;
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
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static bool snake_occupies(const snake_game_t *g, int8_t x, int8_t y) {
  for (int i = 0; i < g->len; i++) {
    if (g->body[i].x == x && g->body[i].y == y)
      return true;
  }
  return false;
}

static void snake_spawn_food(snake_game_t *g) {
  for (int tries = 0; tries < 200; tries++) {
    int8_t fx = (int8_t)(rand() % SNAKE_COLS);
    int8_t fy = (int8_t)(rand() % SNAKE_ROWS);
    if (!snake_occupies(g, fx, fy)) {
      g->food.x = fx;
      g->food.y = fy;
      return;
    }
  }
  for (int8_t y = 0; y < SNAKE_ROWS; y++) {
    for (int8_t x = 0; x < SNAKE_COLS; x++) {
      if (!snake_occupies(g, x, y)) {
        g->food.x = x;
        g->food.y = y;
        return;
      }
    }
  }
}

static void snake_reset(snake_game_t *g) {
  g->len = 4;
  int8_t sx = SNAKE_COLS / 2;
  int8_t sy = SNAKE_ROWS / 2;
  for (int i = 0; i < g->len; i++) {
    g->body[i].x = (int8_t)(sx - i);
    g->body[i].y = sy;
  }
  g->dir_x = 1;
  g->dir_y = 0;
  g->next_dx = 1;
  g->next_dy = 0;
  g->score = 0;
  g->paused = false;
  g->game_over = false;
  snake_spawn_food(g);
}

static void snake_sync_ui(snake_game_t *g) {
  if (g->food_obj) {
    lv_obj_set_pos(g->food_obj, g->food.x * SNAKE_CELL_SIZE + 1,
                   g->food.y * SNAKE_CELL_SIZE + 1);
  }

  for (int i = 0; i < SNAKE_MAX_LEN; i++) {
    if (i < g->len) {
      if (!g->seg_objs[i]) {
        lv_obj_t *seg = lv_obj_create(g->board);
        lv_obj_set_size(seg, SNAKE_CELL_SIZE - 2, SNAKE_CELL_SIZE - 2);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_radius(seg, 2, 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        g->seg_objs[i] = seg;
      }
      lv_obj_clear_flag(g->seg_objs[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(g->seg_objs[i], g->body[i].x * SNAKE_CELL_SIZE + 1,
                     g->body[i].y * SNAKE_CELL_SIZE + 1);
      lv_obj_set_style_bg_color(
          g->seg_objs[i],
          (i == 0) ? lv_color_hex(0x34D399) : lv_color_hex(0x10B981), 0);
    } else if (g->seg_objs[i]) {
      lv_obj_add_flag(g->seg_objs[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  char sbuf[64];
  snprintf(sbuf, sizeof(sbuf), "Score: %d   Best: %d   [%s]", g->score,
           g->high_score, g->fast_mode ? "FAST" : "NORM");
  lv_label_set_text(g->score_lbl, sbuf);

  if (g->game_over) {
    lv_label_set_text(g->status_lbl, "GAME OVER! Press A/ENTER to retry, B:Exit");
  } else if (g->paused) {
    lv_label_set_text(g->status_lbl, "PAUSED — Press A/ENTER to resume, B:Exit");
  } else {
    lv_label_set_text(g->status_lbl, "Arrows:Steer A:Pause C:Speed B/ESC:Quit");
  }
}

static void snake_tick(snake_game_t *g) {
  if (g->paused || g->game_over)
    return;

  g->dir_x = g->next_dx;
  g->dir_y = g->next_dy;

  int8_t nx = (int8_t)(g->body[0].x + g->dir_x);
  int8_t ny = (int8_t)(g->body[0].y + g->dir_y);

  if (nx < 0 || nx >= SNAKE_COLS || ny < 0 || ny >= SNAKE_ROWS ||
      snake_occupies(g, nx, ny)) {
    g->game_over = true;
    snake_sync_ui(g);
    return;
  }

  bool ate = (nx == g->food.x && ny == g->food.y);
  int new_len = g->len + (ate ? 1 : 0);
  if (new_len > SNAKE_MAX_LEN)
    new_len = SNAKE_MAX_LEN;

  for (int i = new_len - 1; i > 0; i--) {
    g->body[i] = g->body[i - 1];
  }
  g->body[0].x = nx;
  g->body[0].y = ny;
  g->len = new_len;

  if (ate) {
    g->score += 10;
    if (g->score > g->high_score) {
      g->high_score = g->score;
    }
    snake_spawn_food(g);
  }

  snake_sync_ui(g);
}

int lvgl_snake_main(int argc, char **argv) {
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
    printf("lvgl_snake: failed to init LVGL\n");
    return -1;
  }

  snake_game_t *g = calloc(1, sizeof(snake_game_t));
  if (!g) {
    eos_lvgl_deinit();
    return -1;
  }

  srand((unsigned)now_ms());

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1120), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  g->score_lbl = lv_label_create(scr);
  lv_obj_set_style_text_color(g->score_lbl, lv_color_hex(0xFDE047), 0);
  lv_obj_align(g->score_lbl, LV_ALIGN_TOP_MID, 0, 6);

  g->board = lv_obj_create(scr);
  lv_obj_set_size(g->board, SNAKE_COLS * SNAKE_CELL_SIZE + 4,
                  SNAKE_ROWS * SNAKE_CELL_SIZE + 4);
  lv_obj_align(g->board, LV_ALIGN_CENTER, 0, -2);
  lv_obj_set_style_bg_color(g->board, lv_color_hex(0x111827), 0);
  lv_obj_set_style_border_color(g->board, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_border_width(g->board, 2, 0);
  lv_obj_set_style_radius(g->board, 4, 0);
  lv_obj_set_style_pad_all(g->board, 0, 0);
  lv_obj_clear_flag(g->board, LV_OBJ_FLAG_SCROLLABLE);

  g->food_obj = lv_obj_create(g->board);
  lv_obj_set_size(g->food_obj, SNAKE_CELL_SIZE - 2, SNAKE_CELL_SIZE - 2);
  lv_obj_set_style_bg_color(g->food_obj, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_border_width(g->food_obj, 0, 0);
  lv_obj_set_style_radius(g->food_obj, SNAKE_CELL_SIZE / 2, 0);
  lv_obj_clear_flag(g->food_obj, LV_OBJ_FLAG_SCROLLABLE);

  g->status_lbl = lv_label_create(scr);
  lv_obj_set_style_text_color(g->status_lbl, lv_color_hex(0x94A3B8), 0);
  lv_obj_align(g->status_lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

  // Standard touch exit/back button
  eos_lvgl_create_back_btn(scr, NULL, NULL);

  snake_reset(g);
  snake_sync_ui(g);

  uint64_t last_step = now_ms();

  while (!g->quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      switch (key) {
      case KEY_UP:
        if (g->dir_y == 0) {
          g->next_dx = 0;
          g->next_dy = -1;
        }
        break;
      case KEY_DOWN:
        if (g->dir_y == 0) {
          g->next_dx = 0;
          g->next_dy = 1;
        }
        break;
      case KEY_LEFT:
        if (g->dir_x == 0) {
          g->next_dx = -1;
          g->next_dy = 0;
        }
        break;
      case KEY_RIGHT:
        if (g->dir_x == 0) {
          g->next_dx = 1;
          g->next_dy = 0;
        }
        break;
      case KEY_A:
      case KEY_ENTER:
      case KEY_SPACE:
        if (g->game_over) {
          snake_reset(g);
        } else {
          g->paused = !g->paused;
        }
        snake_sync_ui(g);
        break;
      case KEY_C:
        g->fast_mode = !g->fast_mode;
        snake_sync_ui(g);
        break;
      case KEY_B:
      case KEY_ESC:
        g->quit = true;
        break;
      default:
        break;
      }
    }

    uint64_t cur = now_ms();
    uint64_t interval = g->fast_mode ? 95ULL : 160ULL;
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
