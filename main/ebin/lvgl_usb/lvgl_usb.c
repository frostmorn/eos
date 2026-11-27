#include "ebin/lvgl_usb/lvgl_usb.h"
#include "ebin/lvgl_usb/usb_logic.h"
#include "ecore/bin.h"
#include "ecore/kcmd.h"
#include "ecore/lvgl.h"
#include "emisc/strlimits.h"

#include <errno.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

enum {
  ACT_ROLE = 1,
  ACT_GADGETS,
  ACT_STORAGE_MODE,
  ACT_MSC_CYCLE,
  ACT_MTP_PATH_CYCLE,
  ACT_DIAG,
  ACT_SET_ROLE_DEBUG,
  ACT_SET_ROLE_COMP,
  ACT_SET_ROLE_HOST,
  ACT_SET_ROLE_OFF,
  ACT_TOGGLE_SERIAL,
  ACT_TOGGLE_MODEM,
  ACT_TOGGLE_MSC,
  ACT_TOGGLE_MTP,
  ACT_SET_PORT0,
  ACT_SET_PORT1,
  ACT_TEST_CDC,
  ACT_TEST_MODEM,
  ACT_REFRESH,
};

typedef struct {
  usb_logic_ctx_t logic;
  usb_menu_page_t page;
  usb_menu_page_t last_page;
  int port_focus;
  int root_focus;
  lv_obj_t *menu;
  lv_obj_t *dialog;
  eos_menu_item_t items[EOS_LVGL_USB_MAX_ITEMS];
  char details[EOS_LVGL_USB_MAX_ITEMS][EOS_LINE_STR_LEN];
  bool has_port1;
  bool rebuild_pending;
  bool quit;
  bool pending_reboot;
  eos_usb_role_t pending_reboot_role;
} usb_ui_t;

static void usb_ui_rebuild(usb_ui_t *ui);

static const char *role_label(eos_usb_role_t role) {
  switch (role) {
  case EOS_USB_ROLE_DEBUG_USJ:
    return "JTAG + CDC";
  case EOS_USB_ROLE_DEVICE_COMPOSITE:
    return "Composite";
  case EOS_USB_ROLE_HOST:
    return "Host";
  default:
    return "Off";
  }
}

static const char *role_to_kcmd(eos_usb_role_t role) {
  switch (role) {
  case EOS_USB_ROLE_HOST:
    return "host";
  case EOS_USB_ROLE_DEVICE_COMPOSITE:
    return "device";
  case EOS_USB_ROLE_DEBUG_USJ:
    return "debug";
  default:
    return "off";
  }
}

static eos_usb_role_t query_port_role(const char *dev_path, eos_usb_role_t fallback) {
  int fd = open(dev_path, O_RDWR);
  if (fd < 0) return fallback;
  eos_usb_bus_status_t st;
  memset(&st, 0, sizeof(st));
  eos_usb_role_t r = (ioctl(fd, EOS_USB_IOCTL_GET_STATUS, &st) == 0) ? st.active_role : fallback;
  close(fd);
  return r;
}

static void format_gadget_summary(const usb_logic_ctx_t *l, char *buf, size_t sz) {
  buf[0] = '\0';
  if (l->gadget_mask & EOS_USB_GADGET_FUNC_SERIAL) strlcat(buf, "CDC ", sz);
  if (l->gadget_mask & EOS_USB_GADGET_FUNC_MODEM) strlcat(buf, "Modem ", sz);
  if (l->gadget_mask & EOS_USB_GADGET_FUNC_MSC) strlcat(buf, "MSC ", sz);
  if (l->gadget_mask & EOS_USB_GADGET_FUNC_MTP) strlcat(buf, "MTP", sz);
  if (!buf[0]) strlcpy(buf, "None", sz);
}

static void add_item(usb_ui_t *ui, size_t *idx, int id, const char *icon,
                     const char *label, const char *detail) {
  if (*idx >= EOS_LVGL_USB_MAX_ITEMS) return;
  strlcpy(ui->details[*idx], detail ? detail : "", sizeof(ui->details[*idx]));
  ui->items[*idx] = (eos_menu_item_t){
      .id = id,
      .icon = icon,
      .label = label,
      .detail = ui->details[*idx],
  };
  (*idx)++;
}

static void menu_cancel_cb(eos_menu_t *menu, void *user_data) {
  (void)menu;
  usb_ui_t *ui = (usb_ui_t *)user_data;
  if (!ui) return;
  if (ui->page == USB_MENU_PORT || (ui->page == USB_MENU_ROOT && !ui->has_port1)) {
    ui->quit = true;
  } else if (ui->page == USB_MENU_ROOT) {
    ui->page = USB_MENU_PORT;
    ui->rebuild_pending = true;
  } else {
    ui->page = USB_MENU_ROOT;
    ui->rebuild_pending = true;
  }
}

static void handle_root_select(usb_ui_t *ui, int id) {
  if (id == ACT_ROLE) ui->page = USB_MENU_ROLE;
  else if (id == ACT_GADGETS) ui->page = USB_MENU_GADGETS;
  else if (id == ACT_STORAGE_MODE) {
    bool is_mtp = (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MTP) != 0;
    usb_logic_set_storage_mode(&ui->logic, !is_mtp);
  }
  else if (id == ACT_MSC_CYCLE) usb_logic_cycle_msc_device(&ui->logic);
  else if (id == ACT_MTP_PATH_CYCLE) {
    const char *next_path = (strcmp(ui->logic.mtp_path, "/") == 0) ? "/mnt/sd" : "/";
    usb_logic_set_mtp_path(&ui->logic, next_path);
  }
  else if (id == ACT_DIAG) ui->page = USB_MENU_DIAG;
  ui->rebuild_pending = true;
}

static void handle_port_select(usb_ui_t *ui, int id) {
  if (id == ACT_SET_PORT0) {
    usb_logic_select_dev(&ui->logic, "/dev/usb0");
  } else if (id == ACT_SET_PORT1) {
    usb_logic_select_dev(&ui->logic, "/dev/usb1");
  }
  ui->page = USB_MENU_ROOT;
  ui->root_focus = 0;
  ui->rebuild_pending = true;
}

static void role_reboot_confirm_cb(bool confirmed, void *user_data) {
  usb_ui_t *ui = (usb_ui_t *)user_data;
  if (!confirmed || !ui) return;
  ui->pending_reboot = true;
  ui->quit = true;
}

static bool apply_role_or_prompt(usb_ui_t *ui, eos_usb_role_t role) {
  if (role == ui->logic.active_role) {
    return false;
  }
  if (ui->logic.status.port_id > 0) {
    usb_logic_set_role(&ui->logic, role);
    return false;
  }
  ui->pending_reboot_role = role;
  eos_lvgl_confirm_dialog_cfg_t cfg = {
      .title = "Reboot Required",
      .message = "Changing the USB port role requires a reboot. Reboot now?",
      .confirm_label = "Reboot",
      .cancel_label = "Cancel",
      .dangerous = false,
      .cb = role_reboot_confirm_cb,
      .user_data = ui,
  };
  ui->dialog = eos_lvgl_dialog_confirm(&cfg);
  return true;
}

static void handle_sub_select(usb_ui_t *ui, int id) {
  bool opened_dialog = false;
  if (id == ACT_SET_ROLE_DEBUG) opened_dialog = apply_role_or_prompt(ui, EOS_USB_ROLE_DEBUG_USJ);
  else if (id == ACT_SET_ROLE_COMP) opened_dialog = apply_role_or_prompt(ui, EOS_USB_ROLE_DEVICE_COMPOSITE);
  else if (id == ACT_SET_ROLE_HOST) opened_dialog = apply_role_or_prompt(ui, EOS_USB_ROLE_HOST);
  else if (id == ACT_SET_ROLE_OFF) opened_dialog = apply_role_or_prompt(ui, EOS_USB_ROLE_OFF);
  else if (id == ACT_TOGGLE_SERIAL) {
    bool en = (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_SERIAL) == 0;
    usb_logic_toggle_gadget(&ui->logic, EOS_USB_GADGET_FUNC_SERIAL, en);
  } else if (id == ACT_TOGGLE_MODEM) {
    bool en = (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MODEM) == 0;
    usb_logic_toggle_gadget(&ui->logic, EOS_USB_GADGET_FUNC_MODEM, en);
  } else if (id == ACT_TOGGLE_MSC) {
    bool en = (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MSC) == 0;
    usb_logic_toggle_gadget(&ui->logic, EOS_USB_GADGET_FUNC_MSC, en);
  } else if (id == ACT_TOGGLE_MTP) {
    bool en = (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MTP) == 0;
    usb_logic_toggle_gadget(&ui->logic, EOS_USB_GADGET_FUNC_MTP, en);
  } else if (id == ACT_TEST_CDC) {
    usb_logic_run_serial_echo(&ui->logic);
  } else if (id == ACT_TEST_MODEM) {
    usb_logic_run_modem_ping(&ui->logic, "ATI");
  } else if (id == ACT_REFRESH) {
    usb_logic_refresh(&ui->logic);
  }
  if (!opened_dialog) {
    ui->rebuild_pending = true;
  }
}

static void menu_select_cb(eos_menu_t *menu, int item_id, void *user_data) {
  (void)menu;
  usb_ui_t *ui = (usb_ui_t *)user_data;
  if (!ui) return;
  if (ui->page == USB_MENU_PORT) {
    handle_port_select(ui, item_id);
  } else if (ui->page == USB_MENU_ROOT) {
    handle_root_select(ui, item_id);
  } else {
    handle_sub_select(ui, item_id);
  }
}

static size_t build_root_items(usb_ui_t *ui) {
  size_t n = 0;
  char gbuf[EOS_LINE_STR_LEN];
  format_gadget_summary(&ui->logic, gbuf, sizeof(gbuf));
  add_item(ui, &n, ACT_ROLE, LV_SYMBOL_SETTINGS, "USB Role", role_label(ui->logic.active_role));
  add_item(ui, &n, ACT_GADGETS, LV_SYMBOL_LIST, "Functions", gbuf);
  const char *storage_desc = (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MTP)
                                 ? "MTP (Shared)"
                                 : ((ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MSC) ? "MSC (Exclusive)" : "Off");
  add_item(ui, &n, ACT_STORAGE_MODE, LV_SYMBOL_SD_CARD, "Storage Mode", storage_desc);
  if (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MSC) {
    add_item(ui, &n, ACT_MSC_CYCLE, LV_SYMBOL_DRIVE, "MSC Device",
             ui->logic.msc_backing[0] ? ui->logic.msc_backing : "None");
  } else if (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MTP) {
    add_item(ui, &n, ACT_MTP_PATH_CYCLE, LV_SYMBOL_DIRECTORY, "MTP Path",
             ui->logic.mtp_path[0] ? ui->logic.mtp_path : "/mnt/sd");
  }
  add_item(ui, &n, ACT_DIAG, LV_SYMBOL_REFRESH, "Diagnostics", "Run tests");
  return n;
}

static size_t build_sub_items(usb_ui_t *ui, const char **title) {
  size_t n = 0;
  if (ui->page == USB_MENU_ROLE) {
    *title = "USB Role";
    add_item(ui, &n, ACT_SET_ROLE_DEBUG, LV_SYMBOL_SETTINGS, "JTAG + CDC",
             ui->logic.active_role == EOS_USB_ROLE_DEBUG_USJ ? "Active" : "");
    add_item(ui, &n, ACT_SET_ROLE_COMP, LV_SYMBOL_USB, "Composite Device",
             ui->logic.active_role == EOS_USB_ROLE_DEVICE_COMPOSITE ? "Active" : "");
    add_item(ui, &n, ACT_SET_ROLE_HOST, LV_SYMBOL_DRIVE, "USB Host",
             ui->logic.active_role == EOS_USB_ROLE_HOST ? "Active" : "");
    add_item(ui, &n, ACT_SET_ROLE_OFF, LV_SYMBOL_POWER, "Off",
             ui->logic.active_role == EOS_USB_ROLE_OFF ? "Active" : "");
  } else if (ui->page == USB_MENU_GADGETS) {
    *title = "USB Functions";
    add_item(ui, &n, ACT_TOGGLE_SERIAL, LV_SYMBOL_CALL, "CDC Serial",
             (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_SERIAL) ? "ON" : "OFF");
    add_item(ui, &n, ACT_TOGGLE_MODEM, LV_SYMBOL_WIFI, "CDC Modem",
             (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MODEM) ? "ON" : "OFF");
    add_item(ui, &n, ACT_TOGGLE_MSC, LV_SYMBOL_SD_CARD, "Mass Storage",
             (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MSC) ? "ON" : "OFF");
    add_item(ui, &n, ACT_TOGGLE_MTP, LV_SYMBOL_DIRECTORY, "Media Transfer",
             (ui->logic.gadget_mask & EOS_USB_GADGET_FUNC_MTP) ? "ON" : "OFF");
  } else if (ui->page == USB_MENU_PORT) {
    *title = "USB Controllers";
    add_item(ui, &n, ACT_SET_PORT0, LV_SYMBOL_USB, "usb0 (Full Speed)",
             role_label(query_port_role("/dev/usb0", EOS_USB_ROLE_OFF)));
    add_item(ui, &n, ACT_SET_PORT1, LV_SYMBOL_USB, "usb1 (High Speed)",
             role_label(query_port_role("/dev/usb1", EOS_USB_ROLE_OFF)));
  } else {
    *title = "Diagnostics";
    const char *sres = !ui->logic.last_test_serial_valid ? "Tap/A to run"
                       : (ui->logic.serial_test_passed ? "OK" : "FAIL");
    const char *mres = !ui->logic.last_test_modem_valid ? "Tap/A to run"
                       : ui->logic.modem_test_response;
    add_item(ui, &n, ACT_TEST_CDC, LV_SYMBOL_CALL, "Test CDC Serial", sres);
    add_item(ui, &n, ACT_TEST_MODEM, LV_SYMBOL_WIFI, "Ping AT Modem", mres);
    add_item(ui, &n, ACT_REFRESH, LV_SYMBOL_REFRESH, "Refresh Status", "");
  }
  return n;
}

static void usb_ui_rebuild(usb_ui_t *ui) {
  int prev_focus = ui->menu ? eos_menu_get_focused_index(ui->menu) : 0;
  int target_focus = 0;
  if (ui->menu) {
    if (ui->page == ui->last_page) {
      target_focus = prev_focus;
    } else if (ui->last_page == USB_MENU_PORT && ui->page == USB_MENU_ROOT) {
      ui->port_focus = prev_focus;
      target_focus = ui->root_focus;
    } else if (ui->last_page == USB_MENU_ROOT && ui->page == USB_MENU_PORT) {
      target_focus = ui->port_focus;
    } else if (ui->last_page == USB_MENU_ROOT && ui->page != USB_MENU_ROOT) {
      ui->root_focus = prev_focus;
      target_focus = 0;
    } else if (ui->page == USB_MENU_ROOT) {
      target_focus = ui->root_focus;
    }
    eos_menu_close(ui->menu);
    ui->menu = NULL;
  }
  ui->last_page = ui->page;

  const char *title = ui->has_port1
                          ? ((strcmp(ui->logic.dev_path, "/dev/usb1") == 0) ? "USB: usb1 (HS)" : "USB: usb0 (FS)")
                          : "USB Settings";
  size_t count = (ui->page == USB_MENU_ROOT) ? build_root_items(ui)
                                             : build_sub_items(ui, &title);

  eos_menu_cfg_t cfg = {
      .title = title,
      .icon = LV_SYMBOL_USB,
      .items = ui->items,
      .item_count = count,
      .on_select = menu_select_cb,
      .on_cancel = menu_cancel_cb,
      .user_data = ui,
  };
  ui->menu = eos_open_menu(&cfg);
  if (ui->menu && target_focus >= 0 && (size_t)target_focus < count) {
    eos_menu_focus_index(ui->menu, target_focus);
  }
}

int lvgl_usb_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_usb: failed to initialize LVGL\n");
    return -1;
  }

  usb_ui_t ui;
  memset(&ui, 0, sizeof(ui));
  int p1_fd = open("/dev/usb1", O_RDWR);
  if (p1_fd >= 0) {
    eos_usb_bus_status_t p1_st;
    if (ioctl(p1_fd, EOS_USB_IOCTL_GET_STATUS, &p1_st) == 0) {
      ui.has_port1 = true;
    }
    close(p1_fd);
  }
  usb_logic_init(&ui.logic);
  ui.page = ui.has_port1 ? USB_MENU_PORT : USB_MENU_ROOT;
  ui.last_page = ui.page;

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);

  usb_ui_rebuild(&ui);

  while (!ui.quit) {
    if (ui.dialog && (!lv_obj_is_valid(ui.dialog) || lv_obj_has_flag(ui.dialog, LV_OBJ_FLAG_HIDDEN))) {
      ui.dialog = NULL;
      ui.rebuild_pending = true;
    }
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      if (ui.dialog) {
        eos_lvgl_dialog_handle_key(ui.dialog, key);
      } else if (ui.menu) {
        eos_menu_handle_key(ui.menu, key);
      }
    }
    if (ui.rebuild_pending && !ui.quit) {
      ui.rebuild_pending = false;
      usb_ui_rebuild(&ui);
    }
    eos_lvgl_step(EOS_LVGL_USB_STEP_MS);
  }

  if (ui.menu) {
    eos_menu_close(ui.menu);
    ui.menu = NULL;
  }
  char kcmd_buf[EOS_LINE_STR_LEN] = {0};
  if (ui.pending_reboot) {
    if (ui.has_port1) {
      bool is_p1 = (strcmp(ui.logic.dev_path, "/dev/usb1") == 0);
      eos_usb_role_t r0 = is_p1 ? query_port_role("/dev/usb0", EOS_USB_ROLE_DEBUG_USJ) : ui.pending_reboot_role;
      eos_usb_role_t r1 = is_p1 ? ui.pending_reboot_role : query_port_role("/dev/usb1", EOS_USB_ROLE_DEVICE_COMPOSITE);
      snprintf(kcmd_buf, sizeof(kcmd_buf), "%s %s", role_to_kcmd(r0), role_to_kcmd(r1));
    } else {
      strlcpy(kcmd_buf, role_to_kcmd(ui.pending_reboot_role), sizeof(kcmd_buf));
    }
  }
  usb_logic_close(&ui.logic);
  eos_lvgl_deinit();
  if (ui.pending_reboot) {
    eos_reboot_with_kcmd(kcmd_buf);
  }
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_usb_app = {
    EOS_BIN_INITIALIZER,
    .flags       = EOS_BIN_FLAG_GUI,
    .stack_size  = EOS_BIN_GUI_STACK_SIZE,
    .filename    = "lvgl_usb",
    .name        = "USB Settings",
    .group       = "Settings",
    .description = "Configure USB Dual-Mode roles, composite gadgets, and diagnostics",
    .entry_point = lvgl_usb_main,
};
