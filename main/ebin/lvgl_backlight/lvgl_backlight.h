#pragma once

#include "backlight_logic.h"

#define EOS_BACKLIGHT_MENU_MAX     (EOS_BACKLIGHT_PRESET_COUNT + 3)
#define EOS_BACKLIGHT_STEP_MS      10

int lvgl_backlight_main(int argc, char **argv);
