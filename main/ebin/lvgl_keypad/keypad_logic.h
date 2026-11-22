#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ecore/input.h"
#include "emisc/strlimits.h"

#define KEY_CAP_COUNT 10

typedef struct {
  uint16_t code;
  const char *label;
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} key_cap_def_t;

typedef struct {
  uint16_t last_key;
  uint32_t press_count;
} keypad_tracker_t;

const key_cap_def_t *keypad_logic_get_caps(void);
int keypad_logic_find_cap_idx(uint16_t code);
bool keypad_logic_is_exit_key(uint16_t key);
void keypad_logic_record_press(keypad_tracker_t *tracker, uint16_t key);
void keypad_logic_format_key_press(uint16_t key, char *buf, size_t buflen);
void keypad_logic_format_stats(uint32_t count, uint32_t lvgl_key, char *buf, size_t buflen);
