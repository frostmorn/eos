#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
// Graphical backlight control application header
// (o_O)==\~

#define EOS_BACKLIGHT_PRESET_COUNT 5
#define EOS_BACKLIGHT_DEFAULT_STEP 5
#define EOS_BACKLIGHT_FADE_MS      200
#define EOS_BACKLIGHT_MIN_W        160
#define EOS_BACKLIGHT_PADDING      8
#define EOS_BACKLIGHT_MAX_DUTY     100

int lvgl_backlight_main(int argc, char **argv);
