#pragma once

#include "sysmon_logic.h"

#define SYSMON_TITLE_Y             36
#define SYSMON_BAR_DRAM_Y          56
#define SYSMON_PSRAM_LBL_Y         84
#define SYSMON_BAR_PSRAM_Y         104
#define SYSMON_DMA_LBL_Y           132
#define SYSMON_BAR_DMA_Y           152
#define SYSMON_STATS_Y             184
#define SYSMON_BAR_MARGIN          20
#define SYSMON_BAR_H               14
#define SYSMON_SCROLL_STEP         24
#define SYSMON_REFRESH_INTERVAL_MS 250ULL

int lvgl_sysmon_main(int argc, char **argv);
