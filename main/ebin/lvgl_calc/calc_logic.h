#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emisc/strlimits.h"

#define CALC_ROWS 4
#define CALC_COLS 4
#define CALC_MAX_INPUT_LIMIT 999999999LL

typedef struct {
  int64_t acc;
  int64_t cur;
  char op;
  bool entering;
  bool error;
} calc_engine_t;

void calc_logic_init(calc_engine_t *ce);
void calc_logic_press_token(calc_engine_t *ce, const char *tok);
void calc_logic_backspace(calc_engine_t *ce);
int64_t calc_logic_get_display_val(const calc_engine_t *ce);
void calc_logic_format_dec(const calc_engine_t *ce, char *buf, size_t buflen);
void calc_logic_format_hex(const calc_engine_t *ce, char *buf, size_t buflen);
void calc_logic_format_expr(const calc_engine_t *ce, char *buf, size_t buflen);
