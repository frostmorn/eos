#include "ebin/lvgl_net/lvgl_net.h"
#include "ebin/lvgl_net/net_logic.h"
#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "emisc/strlimits.h"

#include <stdio.h>
#include <string.h>

enum {
  NET_ACT_PAGE_STA = 1,
  NET_ACT_PAGE_AP,
  NET_ACT_PAGE_BRIDGE,
  NET_ACT_PAGE_IFACES,
  NET_ACT_PAGE_SNIFFER,
  NET_ACT_RADIO,
  NET_ACT_DHCPC,
  NET_ACT_STATIC_IP,
  NET_ACT_DISCONNECT,
  NET_ACT_SCAN,
  NET_ACT_MANUAL_CONNECT,
  NET_ACT_AP_TOGGLE,
  NET_ACT_DHCPS,
  NET_ACT_AP_SSID,
  NET_ACT_AP_PASS,
  NET_ACT_AP_CHAN,
  NET_ACT_AP_SEC,
  NET_ACT_NAPT,
  NET_ACT_PROMISC,
  NET_ACT_REFRESH,
  NET_ACT_IFACE_BASE = 100,
  NET_ACT_AP_BASE = 200,
};

typedef struct {
  net_logic_t logic;
  net_page_t page;
  lv_obj_t *menu;
  lv_obj_t *dialog_overlay;
  eos_menu_item_t items[NET_MENU_MAX_ITEMS];
  char labels[NET_MENU_MAX_ITEMS][EOS_LINE_STR_LEN];
  char details[NET_MENU_MAX_ITEMS][EOS_LINE_STR_LEN];
  bool rebuild_pending;
} net_app_t;

static void net_rebuild_menu(net_app_t *app);
static void net_open_dialog(net_app_t *app, net_dialog_type_t type, const char *title,
                            const char *prompt, const char *initial, bool is_pw);

static void add_net_item(net_app_t *app, size_t *idx, int id, const char *icon,
                         const char *label, const char *detail) {
  if (*idx >= NET_MENU_MAX_ITEMS) return;
  strlcpy(app->labels[*idx], label ? label : "", sizeof(app->labels[*idx]));
  strlcpy(app->details[*idx], detail ? detail : "", sizeof(app->details[*idx]));
  app->items[*idx] = (eos_menu_item_t){
      .id = id,
      .icon = icon,
      .label = app->labels[*idx],
      .detail = app->details[*idx],
  };
  (*idx)++;
}

static void net_dialog_cancel_cb(const char *text, void *user_data) {
  (void)text;
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;
  app->logic.dialog_active = false;
  app->logic.dialog_type = DIALOG_NONE;
  app->dialog_overlay = NULL;
  app->rebuild_pending = true;
}

static void net_dialog_submit_cb(const char *text, void *user_data) {
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;

  net_dialog_type_t dtype = app->logic.dialog_type;
  app->logic.dialog_active = false;
  app->logic.dialog_type = DIALOG_NONE;
  app->dialog_overlay = NULL;

  if (dtype == DIALOG_CONNECT_AP_PW && text) {
    int idx = app->logic.dialog_ap_idx;
    if (idx >= 0 && idx < app->logic.ap_count) {
      net_logic_connect_ap(&app->logic, app->logic.aps[idx].ssid, text);
    }
  } else if (dtype == DIALOG_MANUAL_SSID && text && text[0]) {
    strlcpy(app->logic.connect_ssid, text, sizeof(app->logic.connect_ssid));
    net_open_dialog(app, DIALOG_MANUAL_PW, "Manual Connect", "Enter Password (empty=Open):", "", true);
    return;
  } else if (dtype == DIALOG_MANUAL_PW) {
    net_logic_connect_ap(&app->logic, app->logic.connect_ssid, text ? text : "");
  } else if (dtype == DIALOG_AP_SSID && text && text[0]) {
    net_logic_set_ap_ssid(&app->logic, text);
  } else if (dtype == DIALOG_AP_PASS && text) {
    net_logic_set_ap_pass(&app->logic, text);
  } else if (dtype == DIALOG_STA_STATIC_IP && text && text[0]) {
    net_logic_set_sta_static_ip(&app->logic, text);
  }
  app->rebuild_pending = true;
}

static void net_open_dialog(net_app_t *app, net_dialog_type_t type, const char *title,
                            const char *prompt, const char *initial, bool is_pw) {
  if (!app || app->logic.dialog_active) return;
  app->logic.dialog_active = true;
  app->logic.dialog_type = type;

  eos_lvgl_input_dialog_cfg_t cfg = {
      .title = title,
      .prompt = prompt,
      .initial_text = initial,
      .ok_label = "OK",
      .is_password = is_pw,
      .ok_cb = net_dialog_submit_cb,
      .cancel_cb = net_dialog_cancel_cb,
      .user_data = app,
  };
  app->dialog_overlay = eos_lvgl_create_input_dialog(&cfg);
}

static void net_menu_cancel_cb(eos_menu_t *menu, void *user_data) {
  (void)menu;
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;
  if (app->page == NET_PAGE_ROOT) {
    app->logic.quit = true;
  } else {
    app->page = NET_PAGE_ROOT;
    app->rebuild_pending = true;
  }
}

static void handle_root_act(net_app_t *app, int id) {
  if (id == NET_ACT_PAGE_STA) app->page = NET_PAGE_STA;
  else if (id == NET_ACT_PAGE_AP) app->page = NET_PAGE_AP;
  else if (id == NET_ACT_PAGE_BRIDGE) app->page = NET_PAGE_BRIDGE;
  else if (id == NET_ACT_PAGE_IFACES) app->page = NET_PAGE_IFACES;
  else if (id == NET_ACT_PAGE_SNIFFER) app->page = NET_PAGE_SNIFFER;
  app->rebuild_pending = true;
}

static void handle_sta_act(net_app_t *app, int id) {
  if (id == NET_ACT_RADIO) net_logic_toggle_radio(&app->logic);
  else if (id == NET_ACT_DHCPC) net_logic_toggle_dhcpc(&app->logic);
  else if (id == NET_ACT_STATIC_IP) {
    net_open_dialog(app, DIALOG_STA_STATIC_IP, "Static IP", "Enter IP (e.g. 192.168.1.50):", "", false);
    return;
  } else if (id == NET_ACT_DISCONNECT) net_logic_disconnect(&app->logic);
  else if (id == NET_ACT_SCAN) net_logic_scan(&app->logic);
  else if (id == NET_ACT_MANUAL_CONNECT) {
    net_open_dialog(app, DIALOG_MANUAL_SSID, "Manual Connect", "Enter SSID:", "", false);
    return;
  } else if (id >= NET_ACT_AP_BASE && id < NET_ACT_AP_BASE + app->logic.ap_count) {
    int idx = id - NET_ACT_AP_BASE;
    if (app->logic.aps[idx].authmode == 0) {
      net_logic_connect_ap(&app->logic, app->logic.aps[idx].ssid, "");
    } else {
      app->logic.dialog_ap_idx = idx;
      net_open_dialog(app, DIALOG_CONNECT_AP_PW, app->logic.aps[idx].ssid, "Enter Wi-Fi Password:", "", true);
      return;
    }
  }
  app->rebuild_pending = true;
}

static void handle_ap_act(net_app_t *app, int id) {
  if (id == NET_ACT_AP_TOGGLE) net_logic_toggle_ap(&app->logic);
  else if (id == NET_ACT_DHCPS) net_logic_toggle_dhcps(&app->logic);
  else if (id == NET_ACT_AP_SSID) {
    net_open_dialog(app, DIALOG_AP_SSID, "SoftAP SSID", "Enter Hotspot SSID:", app->logic.ap_cfg.ssid, false);
    return;
  } else if (id == NET_ACT_AP_PASS) {
    net_open_dialog(app, DIALOG_AP_PASS, "SoftAP Password", "Enter Password (min 8 chars):", app->logic.ap_cfg.password, true);
    return;
  } else if (id == NET_ACT_AP_CHAN) net_logic_cycle_ap_chan(&app->logic);
  else if (id == NET_ACT_AP_SEC) net_logic_toggle_ap_security(&app->logic);
  app->rebuild_pending = true;
}

static void net_menu_select_cb(eos_menu_t *menu, int item_id, void *user_data) {
  (void)menu;
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;

  if (app->page == NET_PAGE_ROOT) {
    handle_root_act(app, item_id);
  } else if (app->page == NET_PAGE_STA) {
    handle_sta_act(app, item_id);
  } else if (app->page == NET_PAGE_AP) {
    handle_ap_act(app, item_id);
  } else if (app->page == NET_PAGE_BRIDGE && item_id == NET_ACT_NAPT) {
    net_logic_toggle_napt(&app->logic);
    app->rebuild_pending = true;
  } else if (app->page == NET_PAGE_IFACES && item_id >= NET_ACT_IFACE_BASE &&
             item_id < NET_ACT_IFACE_BASE + NET_MAX_IFACES) {
    net_logic_toggle_iface(item_id - NET_ACT_IFACE_BASE);
    app->rebuild_pending = true;
  } else if (app->page == NET_PAGE_SNIFFER && item_id == NET_ACT_PROMISC) {
    net_logic_toggle_promisc(&app->logic);
    app->rebuild_pending = true;
  } else if (item_id == NET_ACT_REFRESH) {
    net_logic_refresh_state(&app->logic);
    app->rebuild_pending = true;
  }
}

static size_t build_root_page(net_app_t *app) {
  size_t n = 0;
  eos_sta_ap_info_t sta_ap;
  const char *sta_d = !app->logic.radio_on ? "Radio OFF"
                      : (net_logic_ioctl_get_sta_ap_info(&sta_ap) && sta_ap.connected
                             ? sta_ap.ssid
                             : "Disconnected");
  add_net_item(app, &n, NET_ACT_PAGE_STA, LV_SYMBOL_WIFI, "Wi-Fi Station", sta_d);
  add_net_item(app, &n, NET_ACT_PAGE_AP, LV_SYMBOL_SETTINGS, "SoftAP Hotspot",
               app->logic.ap_on ? "ON" : "OFF");
  add_net_item(app, &n, NET_ACT_PAGE_BRIDGE, LV_SYMBOL_SHUFFLE, "Network Bridge",
               app->logic.napt_on ? "NAPT ON" : "NAPT OFF");
  add_net_item(app, &n, NET_ACT_PAGE_IFACES, LV_SYMBOL_LIST, "Interfaces", "lo0, sta0, ap0, br0");
  add_net_item(app, &n, NET_ACT_PAGE_SNIFFER, LV_SYMBOL_EYE_OPEN, "802.11 Sniffer",
               app->logic.promisc_on ? "ON" : "OFF");
  return n;
}

static size_t build_sta_page(net_app_t *app) {
  size_t n = 0;
  add_net_item(app, &n, NET_ACT_RADIO, LV_SYMBOL_POWER, "Wi-Fi Radio",
               app->logic.radio_on ? "ON" : "OFF");
  add_net_item(app, &n, NET_ACT_DHCPC, LV_SYMBOL_SETTINGS, "DHCP Client",
               app->logic.sta_dhcp_on ? "ON" : "OFF");
  if (!app->logic.sta_dhcp_on) {
    add_net_item(app, &n, NET_ACT_STATIC_IP, LV_SYMBOL_EDIT, "Set Static IP", "sta0");
  }
  eos_sta_ap_info_t sta_ap;
  if (net_logic_ioctl_get_sta_ap_info(&sta_ap) && sta_ap.connected) {
    add_net_item(app, &n, NET_ACT_DISCONNECT, LV_SYMBOL_CLOSE, "Disconnect", sta_ap.ssid);
  }
  add_net_item(app, &n, NET_ACT_SCAN, LV_SYMBOL_REFRESH, "Scan Networks", "");
  add_net_item(app, &n, NET_ACT_MANUAL_CONNECT, LV_SYMBOL_EDIT, "Manual Connect", "");
  for (int i = 0; i < app->logic.ap_count; i++) {
    char dbuf[EOS_LINE_STR_LEN];
    snprintf(dbuf, sizeof(dbuf), "%ddBm ch%u%s", (int)app->logic.aps[i].rssi,
             (unsigned)app->logic.aps[i].channel,
             app->logic.aps[i].authmode ? " *" : "");
    add_net_item(app, &n, NET_ACT_AP_BASE + i, LV_SYMBOL_WIFI, app->logic.aps[i].ssid, dbuf);
  }
  return n;
}

static size_t build_ap_page(net_app_t *app) {
  size_t n = 0;
  char chbuf[EOS_SMALL_STR_LEN];
  snprintf(chbuf, sizeof(chbuf), "Ch %u", (unsigned)app->logic.ap_cfg.channel);
  add_net_item(app, &n, NET_ACT_AP_TOGGLE, LV_SYMBOL_POWER, "SoftAP",
               app->logic.ap_on ? "ON" : "OFF");
  add_net_item(app, &n, NET_ACT_DHCPS, LV_SYMBOL_SETTINGS, "DHCP Server",
               app->logic.ap_dhcp_on ? "ON" : "OFF");
  add_net_item(app, &n, NET_ACT_AP_SSID, LV_SYMBOL_EDIT, "AP SSID", app->logic.ap_cfg.ssid);
  add_net_item(app, &n, NET_ACT_AP_PASS, LV_SYMBOL_EDIT, "AP Password",
               app->logic.ap_cfg.password[0] ? "********" : "(none)");
  add_net_item(app, &n, NET_ACT_AP_CHAN, LV_SYMBOL_REFRESH, "Channel", chbuf);
  add_net_item(app, &n, NET_ACT_AP_SEC, LV_SYMBOL_SETTINGS, "Security",
               app->logic.ap_cfg.authmode ? "WPA2" : "Open");
  return n;
}

static size_t build_ifaces_page(net_app_t *app) {
  static const char *ifaces[NET_MAX_IFACES] = {"/dev/lo0", "/dev/sta0", "/dev/ap0", "/dev/br0"};
  size_t n = 0;
  for (int i = 0; i < NET_MAX_IFACES; i++) {
    eos_netif_info_t info;
    char dbuf[EOS_LINE_STR_LEN] = "N/A";
    if (net_logic_ioctl_get_netif_info(ifaces[i], &info)) {
      snprintf(dbuf, sizeof(dbuf), "%s (%s)", info.ip.ip,
               (info.flags & EOS_NETIF_FLAG_UP) ? "UP" : "DOWN");
    }
    add_net_item(app, &n, NET_ACT_IFACE_BASE + i, LV_SYMBOL_LIST, ifaces[i], dbuf);
  }
  return n;
}

static void net_rebuild_menu(net_app_t *app) {
  int prev_focus = app->menu ? eos_menu_get_focused_index(app->menu) : 0;
  if (app->menu) {
    eos_menu_close(app->menu);
    app->menu = NULL;
  }

  const char *title = "EOS Network";
  size_t count = 0;
  if (app->page == NET_PAGE_ROOT) {
    count = build_root_page(app);
  } else if (app->page == NET_PAGE_STA) {
    title = "Wi-Fi Station";
    count = build_sta_page(app);
  } else if (app->page == NET_PAGE_AP) {
    title = "SoftAP Hotspot";
    count = build_ap_page(app);
  } else if (app->page == NET_PAGE_BRIDGE) {
    title = "Network Bridge";
    eos_netif_bridge_info_t binfo;
    char bbuf[EOS_LINE_STR_LEN] = "br0";
    if (net_logic_ioctl_get_bridge_info(&binfo)) {
      snprintf(bbuf, sizeof(bbuf), "ports: %u", (unsigned)binfo.port_count);
    }
    add_net_item(app, &count, NET_ACT_NAPT, LV_SYMBOL_SHUFFLE, "NAPT Routing",
                 app->logic.napt_on ? "ON" : "OFF");
    add_net_item(app, &count, NET_ACT_REFRESH, LV_SYMBOL_REFRESH, "Bridge Status", bbuf);
  } else if (app->page == NET_PAGE_IFACES) {
    title = "Interfaces";
    count = build_ifaces_page(app);
  } else {
    title = "802.11 Sniffer";
    add_net_item(app, &count, NET_ACT_PROMISC, LV_SYMBOL_EYE_OPEN, "Promiscuous Mode",
                 app->logic.promisc_on ? "ON" : "OFF");
  }

  eos_menu_cfg_t cfg = {
      .title = title,
      .icon = LV_SYMBOL_WIFI,
      .items = app->items,
      .item_count = count,
      .on_select = net_menu_select_cb,
      .on_cancel = net_menu_cancel_cb,
      .user_data = app,
  };
  app->menu = eos_open_menu(&cfg);
  if (app->menu && prev_focus >= 0 && (size_t)prev_focus < count) {
    eos_menu_focus_index(app->menu, prev_focus);
  }
}

int lvgl_net_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_net: failed to initialize LVGL\n");
    return -1;
  }

  net_app_t app;
  memset(&app, 0, sizeof(app));
  net_logic_init(&app.logic);
  net_logic_refresh_state(&app.logic);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(EOS_THEME_BG_COLOR), 0);

  net_rebuild_menu(&app);

  while (!app.logic.quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      if (app.logic.dialog_active && app.dialog_overlay) {
        eos_lvgl_keyboard_handle_key(key);
      } else if (app.menu) {
        eos_menu_handle_key(app.menu, key);
      }
      if (app.logic.quit) break;
    }
    if (app.rebuild_pending && !app.logic.quit && !app.logic.dialog_active) {
      app.rebuild_pending = false;
      net_logic_refresh_state(&app.logic);
      net_rebuild_menu(&app);
    }
    eos_lvgl_step(NET_STEP_DELAY_MS);
  }

  if (app.menu) {
    eos_menu_close(app.menu);
    app.menu = NULL;
  }
  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_net_app = {
    EOS_BIN_INITIALIZER,
    .flags       = EOS_BIN_FLAG_GUI,
    .stack_size  = EOS_BIN_GUI_STACK_SIZE,
    .filename    = "lvgl_net",
    .name        = "Network",
    .group       = "Settings",
    .description = "Manage Wi-Fi STA, SoftAP, Network Bridge, Interfaces & Sniffer",
    .entry_point = lvgl_net_main,
};
