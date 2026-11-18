#pragma once

#include "ecore/lvgl.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

#define NET_MAX_SCAN_APS 16
#define NET_NUM_TABS     5
#define NET_MAX_ITEMS    48

#define NET_SSID_MAX_LEN     33
#define NET_SCAN_DWELL_MS    120
#define NET_MIN_CONTENT_H    60

// Color Palette (standard EOS theme from ecore/lvgl.h)
#define COL_BG_DEEP       EOS_COLOR_BG_DARK
#define COL_BG_PANEL      EOS_COLOR_BG_PANEL
#define COL_BG_CARD       EOS_COLOR_BG_CARD
#define COL_BORDER        EOS_COLOR_BORDER
#define COL_BORDER_HOVER  EOS_COLOR_BORDER_FOCUS
#define COL_TEXT_PRIMARY  EOS_COLOR_TEXT_PRIMARY
#define COL_TEXT_SECOND   EOS_COLOR_TEXT_MUTED
#define COL_ACCENT_BLUE   EOS_COLOR_ACCENT_BLUE
#define COL_ACCENT_SKY    EOS_COLOR_ACCENT_CYAN
#define COL_ACCENT_LTBLUE EOS_COLOR_BORDER_FOCUS
#define COL_GREEN         0x10B981
#define COL_YELLOW        EOS_COLOR_ACCENT_YELLOW
#define COL_AMBER         EOS_COLOR_STATUS_ORANGE
#define COL_RED           EOS_COLOR_STATUS_RED
#define COL_RED_DARK      0x991B1B
#define COL_HDR_BG        EOS_COLOR_BG_HDR
#define COL_TAB_BG        EOS_COLOR_BG_SUBHDR
#define COL_WHITE         EOS_COLOR_TEXT_WHITE
