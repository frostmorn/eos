#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emisc/strlimits.h"

#define EOS_BACKLIGHT_PRESET_COUNT 5
#define EOS_BACKLIGHT_DEFAULT_STEP 5
#define EOS_BACKLIGHT_FADE_MS      200
#define EOS_BACKLIGHT_MAX_DUTY     100
#define EOS_BACKLIGHT_DEFAULT_DEV  "/dev/" "fb0"

typedef struct {
  int dev_fd;
  bool is_pwm;
  int cur_brightness;
  bool smooth_fade;
} backlight_device_t;

int backlight_logic_init(backlight_device_t *dev);
void backlight_logic_close(backlight_device_t *dev);
int backlight_logic_apply(backlight_device_t *dev, int val);
int backlight_logic_step(backlight_device_t *dev, int delta);
const int *backlight_logic_get_presets(size_t *count);
void backlight_logic_format_label(int brightness, char *buf, size_t buflen);
