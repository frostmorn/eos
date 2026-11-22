#include "keypad_logic.h"

#include <stdio.h>
#include <string.h>

#include "ecore/bin.h"

static const key_cap_def_t s_key_caps[KEY_CAP_COUNT] = {
    {KEY_UP, "UP", 40, 32, 36, 26},
    {KEY_LEFT, "L", 6, 62, 32, 26},
    {KEY_RIGHT, "R", 78, 62, 32, 26},
    {KEY_DOWN, "DN", 40, 92, 36, 26},
    {KEY_ENTER, "SEL", 92, 124, 44, 22},
    {KEY_SPACE, "STA", 144, 124, 44, 22},
    {KEY_C, "C", 188, 32, 36, 26},
    {KEY_D, "D", 150, 62, 36, 26},
    {KEY_A, "A", 226, 62, 36, 26},
    {KEY_B, "B", 188, 92, 36, 26},
};

const key_cap_def_t *keypad_logic_get_caps(void) {
  return s_key_caps;
}

int keypad_logic_find_cap_idx(uint16_t code) {
  for (int i = 0; i < KEY_CAP_COUNT; i++) {
    if (s_key_caps[i].code == code) {
      return i;
    }
  }
  return -1;
}

bool keypad_logic_is_exit_key(uint16_t key) {
  return (key == KEY_ESC || key == KEY_B || key == KEY_X);
}

void keypad_logic_record_press(keypad_tracker_t *tracker, uint16_t key) {
  if (!tracker) return;
  tracker->last_key = key;
  tracker->press_count++;
}

void keypad_logic_format_key_press(uint16_t key, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  int idx = keypad_logic_find_cap_idx(key);
  const char *name = (idx >= 0) ? s_key_caps[idx].label : "OTHER";
  snprintf(buf, buflen, "Key: %s (code %u)", name, (unsigned)key);
}

void keypad_logic_format_stats(uint32_t count, uint32_t lvgl_key, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  snprintf(buf, buflen, "LVGL key: 0x%X   Count: %u", (unsigned)lvgl_key, (unsigned)count);
}
