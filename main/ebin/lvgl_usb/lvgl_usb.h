#pragma once

#include <stdint.h>
#include "ecore/lvgl.h"

#define EOS_LVGL_USB_MAX_ITEMS 8
#define EOS_LVGL_USB_STEP_MS   10

typedef enum {
  USB_MENU_ROOT = 0,
  USB_MENU_ROLE,
  USB_MENU_GADGETS,
  USB_MENU_PORT,
  USB_MENU_DIAG,
} usb_menu_page_t;

int lvgl_usb_main(int argc, char **argv);
