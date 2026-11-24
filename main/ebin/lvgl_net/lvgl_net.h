#pragma once

#include "ecore/lvgl.h"
#include "ebin/lvgl_net/net_logic.h"

#define NET_MENU_MAX_ITEMS  32
#define NET_STEP_DELAY_MS   10
#define NET_SCAN_STEP_MS    20

typedef enum {
  NET_PAGE_ROOT = 0,
  NET_PAGE_STA,
  NET_PAGE_AP,
  NET_PAGE_BRIDGE,
  NET_PAGE_IFACES,
  NET_PAGE_SNIFFER,
} net_page_t;

int lvgl_net_main(int argc, char **argv);
