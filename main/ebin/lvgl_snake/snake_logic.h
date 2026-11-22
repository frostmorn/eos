#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SNAKE_MAX_LEN 512
#define SNAKE_MAX_FOOD_TRIES 200

typedef struct {
  int8_t x;
  int8_t y;
} snake_pt_t;

typedef struct {
  int cols;
  int rows;
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
} snake_logic_t;

void snake_logic_init(snake_logic_t *s, int cols, int rows);
void snake_logic_reset(snake_logic_t *s);
void snake_logic_spawn_food(snake_logic_t *s);
bool snake_logic_occupies(const snake_logic_t *s, int8_t x, int8_t y);
bool snake_logic_tick(snake_logic_t *s);
void snake_logic_set_direction(snake_logic_t *s, int8_t dx, int8_t dy);
void snake_logic_toggle_pause(snake_logic_t *s);
void snake_logic_toggle_speed(snake_logic_t *s);
