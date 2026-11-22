#include "snake_logic.h"

#include <stdlib.h>
#include <string.h>

bool snake_logic_occupies(const snake_logic_t *s, int8_t x, int8_t y) {
  if (!s) return false;
  for (int i = 0; i < s->len; i++) {
    if (s->body[i].x == x && s->body[i].y == y) {
      return true;
    }
  }
  return false;
}

void snake_logic_spawn_food(snake_logic_t *s) {
  if (!s) return;
  int cols = s->cols > 0 ? s->cols : 18;
  int rows = s->rows > 0 ? s->rows : 14;

  for (int tries = 0; tries < SNAKE_MAX_FOOD_TRIES; tries++) {
    int8_t fx = (int8_t)(rand() % cols);
    int8_t fy = (int8_t)(rand() % rows);
    if (!snake_logic_occupies(s, fx, fy)) {
      s->food.x = fx;
      s->food.y = fy;
      return;
    }
  }

  for (int8_t y = 0; y < rows; y++) {
    for (int8_t x = 0; x < cols; x++) {
      if (!snake_logic_occupies(s, x, y)) {
        s->food.x = x;
        s->food.y = y;
        return;
      }
    }
  }
}

void snake_logic_reset(snake_logic_t *s) {
  if (!s) return;
  int cols = s->cols > 0 ? s->cols : 18;
  int rows = s->rows > 0 ? s->rows : 14;

  s->len = 4;
  int8_t sx = (int8_t)(cols / 2);
  int8_t sy = (int8_t)(rows / 2);

  for (int i = 0; i < s->len; i++) {
    s->body[i].x = (int8_t)(sx - i);
    s->body[i].y = sy;
  }

  s->dir_x = 1;
  s->dir_y = 0;
  s->next_dx = 1;
  s->next_dy = 0;
  s->score = 0;
  s->paused = false;
  s->game_over = false;

  snake_logic_spawn_food(s);
}

void snake_logic_init(snake_logic_t *s, int cols, int rows) {
  if (!s) return;
  memset(s, 0, sizeof(*s));
  s->cols = cols;
  s->rows = rows;
  snake_logic_reset(s);
}

bool snake_logic_tick(snake_logic_t *s) {
  if (!s || s->paused || s->game_over) {
    return false;
  }

  s->dir_x = s->next_dx;
  s->dir_y = s->next_dy;

  int8_t nx = (int8_t)(s->body[0].x + s->dir_x);
  int8_t ny = (int8_t)(s->body[0].y + s->dir_y);

  if (nx < 0 || nx >= s->cols || ny < 0 || ny >= s->rows ||
      snake_logic_occupies(s, nx, ny)) {
    s->game_over = true;
    return true;
  }

  bool ate = (nx == s->food.x && ny == s->food.y);
  int new_len = s->len + (ate ? 1 : 0);
  if (new_len > SNAKE_MAX_LEN) {
    new_len = SNAKE_MAX_LEN;
  }

  for (int i = new_len - 1; i > 0; i--) {
    s->body[i] = s->body[i - 1];
  }
  s->body[0].x = nx;
  s->body[0].y = ny;
  s->len = new_len;

  if (ate) {
    s->score += 10;
    if (s->score > s->high_score) {
      s->high_score = s->score;
    }
    snake_logic_spawn_food(s);
  }

  return true;
}

void snake_logic_set_direction(snake_logic_t *s, int8_t dx, int8_t dy) {
  if (!s) return;
  // Disallow 180 degree reversal
  if ((dx != 0 && s->dir_x == -dx) || (dy != 0 && s->dir_y == -dy)) {
    return;
  }
  s->next_dx = dx;
  s->next_dy = dy;
}

void snake_logic_toggle_pause(snake_logic_t *s) {
  if (!s) return;
  if (s->game_over) {
    snake_logic_reset(s);
  } else {
    s->paused = !s->paused;
  }
}

void snake_logic_toggle_speed(snake_logic_t *s) {
  if (!s) return;
  s->fast_mode = !s->fast_mode;
}
