#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/lvgl.h"
#include "ecore/netif.h"
#include "edriver/net/net_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define NET_MAX_SCAN_APS 16
#define NET_NUM_TABS     5

typedef enum {
  TAB_STA = 0,
  TAB_AP,
  TAB_BRIDGE,
  TAB_IFACES,
  TAB_SNIFFER,
} net_tab_t;

typedef struct {
  char ssid[33];
  int8_t rssi;
  uint8_t channel;
  uint8_t authmode;
} net_ap_item_t;

typedef struct {
  net_tab_t cur_tab;
  int sel_idx;
  int item_count;
  bool quit;

  // Hardware states
  bool radio_on;
  bool ap_on;
  bool napt_on;
  bool promisc_on;

  // Scan & Connection
  net_ap_item_t aps[NET_MAX_SCAN_APS];
  int ap_count;
  char connect_ssid[33];
  char connect_pass[64];
  char status_msg[128];

  // UI Widgets
  lv_obj_t *hdr;
  lv_obj_t *hdr_title;
  lv_obj_t *tab_bar;
  lv_obj_t *tab_label;
  lv_obj_t *content_cont;
  lv_obj_t *footer;
  lv_obj_t *footer_lbl;

  // Switches (status indicated in switch only)
  lv_obj_t *sw_radio;
  lv_obj_t *sw_ap;
  lv_obj_t *sw_napt;
  lv_obj_t *sw_promisc;

  // Interactive items for D-Pad / Touch selection
  lv_obj_t *items[24];

  uint64_t last_refresh_ms;
} net_app_t;

static net_app_t *s_app = NULL;

static uint64_t net_get_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

// ---------------------------------------------------------------------------
// Hardware helpers
// ---------------------------------------------------------------------------

static bool get_hw_radio_power(void) {
  int fd = open("/dev/wifi0", O_RDWR);
  if (fd < 0) return false;
  int state = 0;
  ioctl(fd, EOS_RADIO_IOCTL_GET_POWER, &state);
  close(fd);
  return state != 0;
}

static void set_hw_radio_power(bool enable) {
  int fd = open("/dev/wifi0", O_RDWR);
  if (fd < 0) return;
  int state = enable ? 1 : 0;
  ioctl(fd, EOS_RADIO_IOCTL_POWER, state);
  close(fd);
}

// ---------------------------------------------------------------------------
// UI Theme & Styling Helpers (Strict EOS Palette)
// ---------------------------------------------------------------------------

static void style_switch(lv_obj_t *sw) {
  lv_obj_set_size(sw, 38, 20);
  // Inactive / OFF state track (Dark Slate)
  lv_obj_set_style_bg_color(sw, lv_color_hex(0x334155), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
  // Active / ON state track (Emerald Green)
  lv_obj_set_style_bg_color(sw, lv_color_hex(0x10B981), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR);
  // Switch Knob (Off-white)
  lv_obj_set_style_bg_color(sw, lv_color_hex(0xF8FAFC), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
}

static lv_obj_t *create_card(lv_obj_t *parent) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(c, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_radius(c, 4, 0);
  lv_obj_set_style_pad_all(c, 4, 0);
  lv_obj_set_style_pad_row(c, 2, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

// Forward declarations
static void net_render_active_tab(net_app_t *app);
static void net_update_selection_highlight(net_app_t *app);

// ---------------------------------------------------------------------------
// Hardware Actions
// ---------------------------------------------------------------------------

static void do_scan(net_app_t *app) {
  snprintf(app->status_msg, sizeof(app->status_msg), "Scanning Wi-Fi networks...");
  net_render_active_tab(app);
  eos_lvgl_step(10);

  int fd = open("/dev/wifi0", O_RDWR);
  if (fd >= 0) {
    eos_wifi_scan_config_t scfg = {.mode = EOS_WIFI_SCAN_ACTIVE, .dwell_time_ms = 120};
    ioctl(fd, EOS_RADIO_IOCTL_SCAN_START, &scfg);

    uint16_t count = 0;
    ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_COUNT, &count);

    eos_wifi_scan_ap_t raw_aps[NET_MAX_SCAN_APS];
    eos_wifi_scan_results_t res = {.aps = raw_aps, .max_count = NET_MAX_SCAN_APS, .count = 0};
    ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_RESULTS, &res);
    close(fd);

    app->ap_count = (int)res.count;
    for (int i = 0; i < app->ap_count; i++) {
      strncpy(app->aps[i].ssid, res.aps[i].ssid[0] ? res.aps[i].ssid : "<Hidden>", 32);
      app->aps[i].rssi = res.aps[i].rssi;
      app->aps[i].channel = res.aps[i].channel;
      app->aps[i].authmode = res.aps[i].authmode;
    }
    snprintf(app->status_msg, sizeof(app->status_msg), "Found %d networks", app->ap_count);
  } else {
    snprintf(app->status_msg, sizeof(app->status_msg), "Error: /dev/wifi0 unavailable");
  }

  app->radio_on = get_hw_radio_power();
  net_render_active_tab(app);
}

static void do_connect_ap(net_app_t *app, const char *ssid) {
  if (!ssid || ssid[0] == '\0') return;
  strncpy(app->connect_ssid, ssid, sizeof(app->connect_ssid) - 1);

  snprintf(app->status_msg, sizeof(app->status_msg), "Connecting to \"%.24s\"...", ssid);
  net_render_active_tab(app);
  eos_lvgl_step(10);

  int fd = open("/dev/sta0", O_RDWR);
  if (fd >= 0) {
    eos_sta_connect_t conn;
    memset(&conn, 0, sizeof(conn));
    strncpy(conn.ssid, ssid, sizeof(conn.ssid) - 1);
    if (app->connect_pass[0]) {
      strncpy(conn.password, app->connect_pass, sizeof(conn.password) - 1);
    }
    int rc = ioctl(fd, EOS_STA_IOCTL_CONNECT, &conn);
    close(fd);

    if (rc == 0) {
      snprintf(app->status_msg, sizeof(app->status_msg), "Connected to \"%.24s\"", ssid);
    } else {
      snprintf(app->status_msg, sizeof(app->status_msg), "Failed to connect to \"%.24s\"", ssid);
    }
  } else {
    snprintf(app->status_msg, sizeof(app->status_msg), "Error: /dev/sta0 unavailable");
  }

  app->radio_on = get_hw_radio_power();
  net_render_active_tab(app);
}

static void do_disconnect(net_app_t *app) {
  int fd = open("/dev/sta0", O_RDWR);
  if (fd >= 0) {
    ioctl(fd, EOS_STA_IOCTL_DISCONNECT);
    close(fd);
    snprintf(app->status_msg, sizeof(app->status_msg), "Disconnected from AP");
  }
  net_render_active_tab(app);
}

static void do_toggle_radio(net_app_t *app) {
  app->radio_on = !app->radio_on;
  set_hw_radio_power(app->radio_on);
  net_render_active_tab(app);
}

static void do_toggle_ap(net_app_t *app) {
  int fd = open("/dev/ap0", O_RDWR);
  if (fd >= 0) {
    app->ap_on = !app->ap_on;
    if (app->ap_on) {
      eos_ap_config_t cfg;
      memset(&cfg, 0, sizeof(cfg));
      strncpy(cfg.ssid, "EOS-Hotspot", sizeof(cfg.ssid) - 1);
      strncpy(cfg.password, "eos12345678", sizeof(cfg.password) - 1);
      cfg.channel = 1;
      cfg.authmode = 2; // WPA2
      cfg.max_connection = 4;
      ioctl(fd, EOS_AP_IOCTL_SET_CONFIG, &cfg);
      ioctl(fd, EOS_NETIF_IOCTL_SET_UP);
    } else {
      ioctl(fd, EOS_NETIF_IOCTL_SET_DOWN);
    }
    close(fd);
  }
  app->radio_on = get_hw_radio_power();
  net_render_active_tab(app);
}

static void do_toggle_napt(net_app_t *app) {
  app->napt_on = !app->napt_on;
  int fd = open("/dev/br0", O_RDWR);
  if (fd >= 0) {
    ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_SET_NAPT, app->napt_on ? 1 : 0);
    close(fd);
  } else {
    eos_net_set_napt_enabled(app->napt_on);
  }
  net_render_active_tab(app);
}

static void do_toggle_promisc(net_app_t *app) {
  app->promisc_on = !app->promisc_on;
  int fd = open("/dev/wifi0", O_RDWR);
  if (fd >= 0) {
    eos_wifi_promisc_cfg_t cfg = {
        .enable = app->promisc_on,
        .filter_mgmt = true,
        .filter_ctrl = true,
        .filter_data = true,
        .filter_misc = true,
    };
    ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &cfg);
    close(fd);
  }
  app->radio_on = get_hw_radio_power();
  net_render_active_tab(app);
}

// ---------------------------------------------------------------------------
// Interactive Item Callbacks
// ---------------------------------------------------------------------------

static void handle_item_action(net_app_t *app, int idx) {
  if (idx < 0 || idx >= app->item_count) return;

  switch (app->cur_tab) {
    case TAB_STA:
      if (idx == 0) {
        do_toggle_radio(app);
      } else if (idx == 1) {
        do_scan(app);
      } else if (idx == 2) {
        do_disconnect(app);
      } else {
        int ap_idx = idx - 3;
        if (ap_idx >= 0 && ap_idx < app->ap_count) {
          do_connect_ap(app, app->aps[ap_idx].ssid);
        }
      }
      break;

    case TAB_AP:
      if (idx == 0) {
        do_toggle_ap(app);
      } else if (idx == 1) {
        do_toggle_ap(app);
      }
      break;

    case TAB_BRIDGE:
      if (idx == 0) {
        do_toggle_napt(app);
      }
      break;

    case TAB_IFACES:
      break;

    case TAB_SNIFFER:
      if (idx == 0) {
        do_toggle_promisc(app);
      }
      break;
  }
}

static void item_click_event_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app) return;
  lv_obj_t *target = lv_event_get_current_target(e);

  for (int i = 0; i < app->item_count; i++) {
    if (app->items[i] == target) {
      app->sel_idx = i;
      net_update_selection_highlight(app);
      handle_item_action(app, i);
      break;
    }
  }
}

static void tab_bar_click_event_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app) return;
  app->cur_tab = (app->cur_tab + 1) % NET_NUM_TABS;
  app->sel_idx = 0;
  net_render_active_tab(app);
}

// ---------------------------------------------------------------------------
// Item Creation Helpers
// ---------------------------------------------------------------------------

static lv_obj_t *add_switch_row(net_app_t *app, lv_obj_t *parent, const char *label_text,
                                lv_obj_t **out_sw, bool state) {
  if (app->item_count >= 24) return NULL;
  int idx = app->item_count++;

  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), 28);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_radius(row, 3, 0);
  lv_obj_set_style_pad_hor(row, 6, 0);
  lv_obj_set_style_pad_ver(row, 2, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, item_click_event_cb, LV_EVENT_CLICKED, app);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *sw = lv_switch_create(row);
  style_switch(sw);
  if (state) {
    lv_obj_add_state(sw, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(sw, LV_STATE_CHECKED);
  }
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE); // Let the row handle tap/click

  if (out_sw) *out_sw = sw;
  app->items[idx] = row;
  return row;
}

static lv_obj_t *add_button_row(net_app_t *app, lv_obj_t *parent, const char *text,
                                const char *badge, uint32_t bg_hex) {
  if (app->item_count >= 24) return NULL;
  int idx = app->item_count++;

  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_set_size(btn, lv_pct(100), 26);
  lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, 3, 0);
  lv_obj_set_style_pad_hor(btn, 6, 0);
  lv_obj_set_style_pad_ver(btn, 2, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn, item_click_event_cb, LV_EVENT_CLICKED, app);

  lv_obj_t *lbl_txt = lv_label_create(btn);
  lv_label_set_text(lbl_txt, text);
  lv_obj_set_style_text_color(lbl_txt, lv_color_hex(0xF8FAFC), 0);
  lv_obj_align(lbl_txt, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_clear_flag(lbl_txt, LV_OBJ_FLAG_CLICKABLE);

  if (badge && badge[0]) {
    lv_obj_t *lbl_b = lv_label_create(btn);
    lv_label_set_text(lbl_b, badge);
    lv_obj_set_style_text_color(lbl_b, lv_color_hex(0x93C5FD), 0);
    lv_obj_align(lbl_b, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(lbl_b, LV_OBJ_FLAG_CLICKABLE);
  }

  app->items[idx] = btn;
  return btn;
}

// ---------------------------------------------------------------------------
// Page Rendering (Ordered: STA -> AP -> Bridge -> Interfaces -> Sniffer)
// ---------------------------------------------------------------------------

static void net_render_active_tab(net_app_t *app) {
  if (!app->content_cont) return;

  lv_obj_clean(app->content_cont);
  memset(app->items, 0, sizeof(app->items));
  app->item_count = 0;

  app->radio_on = get_hw_radio_power();

  // Tab titles
  const char *tab_titles[NET_NUM_TABS] = {
      "1. Wi-Fi (Client)",
      "2. Hotspot (AP)",
      "3. Bridge & Routing",
      "4. Interfaces (Netif)",
      "5. 802.11 Sniffer",
  };
  char tbar_str[64];
  snprintf(tbar_str, sizeof(tbar_str), "<  %s  >  [%d/%d]",
           tab_titles[app->cur_tab], (int)app->cur_tab + 1, NET_NUM_TABS);
  lv_label_set_text(app->tab_label, tbar_str);

  // Status message notice if present
  if (app->status_msg[0]) {
    lv_obj_t *sbar = lv_obj_create(app->content_cont);
    lv_obj_set_size(sbar, lv_pct(100), 20);
    lv_obj_set_style_bg_color(sbar, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_opa(sbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sbar, 0, 0);
    lv_obj_set_style_pad_all(sbar, 2, 0);
    lv_obj_clear_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *slbl = lv_label_create(sbar);
    lv_label_set_text(slbl, app->status_msg);
    lv_obj_set_style_text_color(slbl, lv_color_hex(0xFDE047), 0);
    lv_obj_align(slbl, LV_ALIGN_LEFT_MID, 2, 0);
  }

  // =========================================================================
  // PAGE 1: Wi-Fi Station
  // =========================================================================
  if (app->cur_tab == TAB_STA) {
    // Row 0: Master Wi-Fi Radio switch (status indicated in switch only!)
    add_switch_row(app, app->content_cont, "Wi-Fi Radio Power", &app->sw_radio, app->radio_on);

    // Connection summary card
    int sta_fd = open("/dev/sta0", O_RDWR);
    eos_netif_info_t info;
    memset(&info, 0, sizeof(info));
    if (sta_fd >= 0) {
      ioctl(sta_fd, EOS_NETIF_IOCTL_GET_INFO, &info);
      close(sta_fd);
    }

    lv_obj_t *scard = create_card(app->content_cont);
    lv_obj_t *stitle = lv_label_create(scard);
    if (info.flags & EOS_NETIF_FLAG_LINK_UP) {
      lv_label_set_text(stitle, "Connected to Network");
      lv_obj_set_style_text_color(stitle, lv_color_hex(0x10B981), 0);
    } else if (info.flags & EOS_NETIF_FLAG_UP) {
      lv_label_set_text(stitle, "Station Interface UP (Idle)");
      lv_obj_set_style_text_color(stitle, lv_color_hex(0xF59E0B), 0);
    } else {
      lv_label_set_text(stitle, "Station Interface DOWN");
      lv_obj_set_style_text_color(stitle, lv_color_hex(0x94A3B8), 0);
    }

    lv_obj_t *sdet = lv_label_create(scard);
    char det[256];
    snprintf(det, sizeof(det), "IP: %s  GW: %s\n%.80s",
             info.ip.ip[0] ? info.ip.ip : "0.0.0.0",
             info.ip.gw[0] ? info.ip.gw : "0.0.0.0",
             info.extra[0] ? info.extra : "No active connection");
    lv_label_set_text(sdet, det);
    lv_obj_set_style_text_color(sdet, lv_color_hex(0x94A3B8), 0);

    // Action buttons
    add_button_row(app, app->content_cont, "[ Scan Wi-Fi Networks ]", "", 0x2563EB);
    add_button_row(app, app->content_cont, "[ Disconnect ]", "", 0x991B1B);

    // Discovered networks
    if (app->ap_count > 0) {
      lv_obj_t *lhdr = lv_label_create(app->content_cont);
      lv_label_set_text(lhdr, "Available Networks (Select to Connect):");
      lv_obj_set_style_text_color(lhdr, lv_color_hex(0x38BDF8), 0);

      for (int i = 0; i < app->ap_count; i++) {
        char badge[32];
        const char *sec = (app->aps[i].authmode >= 4) ? "WPA3" : (app->aps[i].authmode >= 2 ? "WPA2" : "Open");
        snprintf(badge, sizeof(badge), "%d dBm  Ch%u  %s",
                 (int)app->aps[i].rssi, (unsigned)app->aps[i].channel, sec);
        add_button_row(app, app->content_cont, app->aps[i].ssid, badge, 0x1E293B);
      }
    }
  }

  // =========================================================================
  // PAGE 2: Access Point (SoftAP)
  // =========================================================================
  else if (app->cur_tab == TAB_AP) {
    int ap_fd = open("/dev/ap0", O_RDWR);
    eos_netif_info_t info;
    memset(&info, 0, sizeof(info));
    eos_ap_sta_list_t stas;
    memset(&stas, 0, sizeof(stas));

    if (ap_fd >= 0) {
      ioctl(ap_fd, EOS_NETIF_IOCTL_GET_INFO, &info);
      ioctl(ap_fd, EOS_AP_IOCTL_GET_STAS, &stas);
      close(ap_fd);
    }
    app->ap_on = (info.flags & EOS_NETIF_FLAG_UP) != 0;

    // Row 0: SoftAP master switch (status indicated in switch only!)
    add_switch_row(app, app->content_cont, "SoftAP Hotspot", &app->sw_ap, app->ap_on);

    // Hotspot configuration card
    lv_obj_t *acard = create_card(app->content_cont);
    lv_obj_t *atitle = lv_label_create(acard);
    lv_label_set_text(atitle, "Hotspot Settings");
    lv_obj_set_style_text_color(atitle, lv_color_hex(0x38BDF8), 0);

    lv_obj_t *adet = lv_label_create(acard);
    char adet_str[256];
    snprintf(adet_str, sizeof(adet_str), "SSID: EOS-Hotspot  (WPA2-PSK)\nPassword: eos12345678  Channel: 1\nIP: %s (DHCP Server)",
             info.ip.ip[0] ? info.ip.ip : "192.168.4.1");
    lv_label_set_text(adet, adet_str);
    lv_obj_set_style_text_color(adet, lv_color_hex(0x94A3B8), 0);

    add_button_row(app, app->content_cont, "[ Apply Hotspot Settings ]", "Ch 1, WPA2", 0x2563EB);

    // Connected clients card
    lv_obj_t *ccard = create_card(app->content_cont);
    lv_obj_t *ctitle = lv_label_create(ccard);
    char c_str[64];
    snprintf(c_str, sizeof(c_str), "Connected Clients: %u", (unsigned)stas.count);
    lv_label_set_text(ctitle, c_str);
    lv_obj_set_style_text_color(ctitle, lv_color_hex(0x38BDF8), 0);

    if (stas.count == 0) {
      lv_obj_t *clbl = lv_label_create(ccard);
      lv_label_set_text(clbl, "No client stations connected.");
      lv_obj_set_style_text_color(clbl, lv_color_hex(0x64748B), 0);
    } else {
      for (int i = 0; i < stas.count; i++) {
        lv_obj_t *clbl = lv_label_create(ccard);
        char mac_str[64];
        snprintf(mac_str, sizeof(mac_str), "#%d MAC: %02X:%02X:%02X:%02X:%02X:%02X (%d dBm)",
                 i + 1, stas.stas[i].mac[0], stas.stas[i].mac[1], stas.stas[i].mac[2],
                 stas.stas[i].mac[3], stas.stas[i].mac[4], stas.stas[i].mac[5], (int)stas.stas[i].rssi);
        lv_label_set_text(clbl, mac_str);
        lv_obj_set_style_text_color(clbl, lv_color_hex(0xF8FAFC), 0);
      }
    }
  }

  // =========================================================================
  // PAGE 3: Bridge & Routing
  // =========================================================================
  else if (app->cur_tab == TAB_BRIDGE) {
    app->napt_on = eos_net_is_napt_enabled();
    // Row 0: NAPT Bridge switch (status indicated in switch only!)
    add_switch_row(app, app->content_cont, "NAPT Routing (Bridge AP -> STA)", &app->sw_napt, app->napt_on);

    lv_obj_t *bcard = create_card(app->content_cont);
    lv_obj_t *btitle = lv_label_create(bcard);
    lv_label_set_text(btitle, "Network Bridge Architecture");
    lv_obj_set_style_text_color(btitle, lv_color_hex(0x38BDF8), 0);

    lv_obj_t *bdesc = lv_label_create(bcard);
    lv_label_set_text(bdesc,
                      "Bridge /dev/br0 routes network packets between the SoftAP (ap0)\n"
                      "and the Station interface (sta0).\n\n"
                      "When NAPT is enabled, devices connected to the SoftAP hotspot\n"
                      "can access the Internet through the active Wi-Fi connection.");
    lv_obj_set_style_text_color(bdesc, lv_color_hex(0x94A3B8), 0);
  }

  // =========================================================================
  // PAGE 4: Interfaces (Netif & Hardware Overview)
  // =========================================================================
  else if (app->cur_tab == TAB_IFACES) {
    // Hardware controller overview card
    lv_obj_t *hcard = create_card(app->content_cont);
    lv_obj_t *htitle = lv_label_create(hcard);
    lv_label_set_text(htitle, "Hardware Controller");
    lv_obj_set_style_text_color(htitle, lv_color_hex(0x38BDF8), 0);

    lv_obj_t *hdesc = lv_label_create(hcard);
    int pu = eos_net_wifi_get_chip_pu();
    if (pu >= 0) {
      char hbuf[128];
      snprintf(hbuf, sizeof(hbuf), "Target: JC4880P443C (ESP32-P4)\nCompanion: External ESP32-C6 (SDIO, PU GPIO %d)", pu);
      lv_label_set_text(hdesc, hbuf);
    } else {
      lv_label_set_text(hdesc, "Target: Lilka v2.3 / Generic\nController: ESP32-S3 Internal Wi-Fi PHY");
    }
    lv_obj_set_style_text_color(hdesc, lv_color_hex(0x94A3B8), 0);

    // Interface detail cards: wifi0, sta0, ap0, br0
    const char *devs[] = {"/dev/wifi0", "/dev/sta0", "/dev/ap0", "/dev/br0"};
    for (int d = 0; d < 4; d++) {
      int ifd = open(devs[d], O_RDWR);
      eos_netif_info_t inf;
      memset(&inf, 0, sizeof(inf));
      if (ifd >= 0) {
        ioctl(ifd, EOS_NETIF_IOCTL_GET_INFO, &inf);
        close(ifd);
      }
      lv_obj_t *card = create_card(app->content_cont);
      lv_obj_t *title = lv_label_create(card);
      char tbuf[64];
      snprintf(tbuf, sizeof(tbuf), "%s  [%s]",
               inf.name[0] ? inf.name : devs[d] + 5,
               (inf.flags & EOS_NETIF_FLAG_LINK_UP) ? "LINK UP" : ((inf.flags & EOS_NETIF_FLAG_UP) ? "UP" : "DOWN"));
      lv_label_set_text(title, tbuf);
      lv_obj_set_style_text_color(title, (inf.flags & EOS_NETIF_FLAG_UP) ? lv_color_hex(0x10B981) : lv_color_hex(0x94A3B8), 0);

      lv_obj_t *desc = lv_label_create(card);
      char dbuf[256];
      if (d == 0) {
        snprintf(dbuf, sizeof(dbuf), "Controller: %.96s", inf.extra[0] ? inf.extra : "radio controller");
      } else if (d == 3) {
        snprintf(dbuf, sizeof(dbuf), "Bridge: %.96s", inf.extra[0] ? inf.extra : "ports [sta0, ap0]");
      } else {
        snprintf(dbuf, sizeof(dbuf), "IP: %s  GW: %s  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 inf.ip.ip[0] ? inf.ip.ip : "0.0.0.0",
                 inf.ip.gw[0] ? inf.ip.gw : "0.0.0.0",
                 inf.mac[0], inf.mac[1], inf.mac[2], inf.mac[3], inf.mac[4], inf.mac[5]);
      }
      lv_label_set_text(desc, dbuf);
      lv_obj_set_style_text_color(desc, lv_color_hex(0x94A3B8), 0);
    }
  }

  // =========================================================================
  // PAGE 5: Sniffer
  // =========================================================================
  else if (app->cur_tab == TAB_SNIFFER) {
    // Row 0: 802.11 Monitor switch (status indicated in switch only!)
    add_switch_row(app, app->content_cont, "802.11 Monitor Mode", &app->sw_promisc, app->promisc_on);

    lv_obj_t *scard = create_card(app->content_cont);
    lv_obj_t *stitle = lv_label_create(scard);
    lv_label_set_text(stitle, "Raw 802.11 Packet Sniffer");
    lv_obj_set_style_text_color(stitle, lv_color_hex(0x38BDF8), 0);

    lv_obj_t *sdesc = lv_label_create(scard);
    lv_label_set_text(sdesc,
                      "Promiscuous mode captures raw 802.11 MAC management, control,\n"
                      "and data frames from /dev/wifi0 via read() and injects via write().");
    lv_obj_set_style_text_color(sdesc, lv_color_hex(0x94A3B8), 0);
  }

  if (app->sel_idx >= app->item_count) {
    app->sel_idx = app->item_count > 0 ? app->item_count - 1 : 0;
  }
  net_update_selection_highlight(app);
}

static void net_update_selection_highlight(net_app_t *app) {
  for (int i = 0; i < app->item_count; i++) {
    lv_obj_t *btn = app->items[i];
    if (!btn) continue;
    if (i == app->sel_idx) {
      lv_obj_set_style_border_color(btn, lv_color_hex(0xFDE047), 0);
      lv_obj_set_style_border_width(btn, 2, 0);
      lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
    } else {
      lv_obj_set_style_border_color(btn, lv_color_hex(0x334155), 0);
      lv_obj_set_style_border_width(btn, 1, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// Hardware Key Handler (Lilka Gamepad / D-Pad / Buttons)
// ---------------------------------------------------------------------------

static void net_handle_key(net_app_t *app, uint16_t key) {
  switch (key) {
    case KEY_UP:
      if (app->item_count > 0) {
        app->sel_idx = (app->sel_idx - 1 + app->item_count) % app->item_count;
        net_update_selection_highlight(app);
      }
      break;

    case KEY_DOWN:
      if (app->item_count > 0) {
        app->sel_idx = (app->sel_idx + 1) % app->item_count;
        net_update_selection_highlight(app);
      }
      break;

    case KEY_LEFT:
    case KEY_C:
      app->cur_tab = (app->cur_tab - 1 + NET_NUM_TABS) % NET_NUM_TABS;
      app->sel_idx = 0;
      net_render_active_tab(app);
      break;

    case KEY_RIGHT:
    case KEY_D:
      app->cur_tab = (app->cur_tab + 1) % NET_NUM_TABS;
      app->sel_idx = 0;
      net_render_active_tab(app);
      break;

    case KEY_ENTER:
    case KEY_A:
      if (app->item_count > 0) {
        handle_item_action(app, app->sel_idx);
      }
      break;

    case KEY_ESC:
    case KEY_B:
      app->quit = true;
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Main Application Entry
// ---------------------------------------------------------------------------

int lvgl_net_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  eos_lvgl_cfg_t cfg = {
      .fb_path = fb_path,
      .kbd_path = kbd_path,
      .partial_lines = 0,
      .force_full_frame = false,
      .dpad_group_nav = false,
      .enable_keyboard = true,
  };

  if (eos_lvgl_init(&cfg) != 0) {
    printf("lvgl_net: failed to init LVGL\n");
    return -1;
  }

  net_app_t *app = calloc(1, sizeof(net_app_t));
  if (!app) {
    eos_lvgl_deinit();
    return -1;
  }
  s_app = app;
  app->cur_tab = TAB_STA;
  app->sel_idx = 0;
  app->radio_on = get_hw_radio_power();

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  // Root screen: strictly EOS dark navy #0B1120
  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1120), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Top Header Bar (height 24, exact match to launcher.c)
  app->hdr = lv_obj_create(scr);
  lv_obj_set_size(app->hdr, (int32_t)w, 24);
  lv_obj_align(app->hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(app->hdr, lv_color_hex(0x1E3A8A), 0);
  lv_obj_set_style_bg_opa(app->hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->hdr, 0, 0);
  lv_obj_set_style_radius(app->hdr, 0, 0);
  lv_obj_set_style_pad_hor(app->hdr, 6, 0);
  lv_obj_set_style_pad_ver(app->hdr, 2, 0);
  lv_obj_clear_flag(app->hdr, LV_OBJ_FLAG_SCROLLABLE);

  app->hdr_title = lv_label_create(app->hdr);
  lv_label_set_text(app->hdr_title, "EOS Network");
  lv_obj_set_style_text_color(app->hdr_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(app->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);

  // Top-right corner is exclusively for standard EOS back button!
  eos_lvgl_create_back_btn(scr, NULL, NULL);

  // 2. Tab Selector Bar (height 24, at y=24, exact match to launcher.c)
  app->tab_bar = lv_obj_create(scr);
  lv_obj_set_size(app->tab_bar, (int32_t)w, 24);
  lv_obj_align(app->tab_bar, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_set_style_bg_color(app->tab_bar, lv_color_hex(0x172554), 0);
  lv_obj_set_style_bg_opa(app->tab_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->tab_bar, 0, 0);
  lv_obj_set_style_radius(app->tab_bar, 0, 0);
  lv_obj_set_style_pad_all(app->tab_bar, 2, 0);
  lv_obj_clear_flag(app->tab_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(app->tab_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(app->tab_bar, tab_bar_click_event_cb, LV_EVENT_CLICKED, app);

  app->tab_label = lv_label_create(app->tab_bar);
  lv_obj_set_style_text_color(app->tab_label, lv_color_hex(0xFDE047), 0);
  lv_obj_center(app->tab_label);
  lv_obj_clear_flag(app->tab_label, LV_OBJ_FLAG_CLICKABLE);

  // 3. Main Content Container (height = h - 24 - 24 - 24 - 4)
  int32_t content_h = (int32_t)h - 24 - 24 - 24 - 4;
  if (content_h < 80) content_h = 80;

  app->content_cont = lv_obj_create(scr);
  lv_obj_set_size(app->content_cont, (int32_t)w - 6, content_h);
  lv_obj_align(app->content_cont, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_color(app->content_cont, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(app->content_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(app->content_cont, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(app->content_cont, 1, 0);
  lv_obj_set_style_radius(app->content_cont, 4, 0);
  lv_obj_set_style_pad_all(app->content_cont, 4, 0);
  lv_obj_set_style_pad_row(app->content_cont, 4, 0);
  lv_obj_set_flex_flow(app->content_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(app->content_cont, LV_DIR_VER);

  // 4. Footer Bar (height 22, at bottom, exact match to launcher.c)
  app->footer = lv_obj_create(scr);
  lv_obj_set_size(app->footer, (int32_t)w - 6, 22);
  lv_obj_align(app->footer, LV_ALIGN_BOTTOM_MID, 0, -1);
  lv_obj_set_style_bg_color(app->footer, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(app->footer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(app->footer, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(app->footer, 1, 0);
  lv_obj_set_style_radius(app->footer, 3, 0);
  lv_obj_set_style_pad_all(app->footer, 2, 0);
  lv_obj_clear_flag(app->footer, LV_OBJ_FLAG_SCROLLABLE);

  app->footer_lbl = lv_label_create(app->footer);
  bool has_kbd = (eos_lvgl_get_indev() != NULL);
  if (has_kbd) {
    lv_label_set_text(app->footer_lbl, "U/D:Item L/R:Tab A:Action B:Exit");
  } else {
    lv_label_set_text(app->footer_lbl, "Tap to toggle | Tap bar < > for tabs | B:Exit");
  }
  lv_obj_set_style_text_color(app->footer_lbl, lv_color_hex(0x94A3B8), 0);
  lv_obj_center(app->footer_lbl);

  // Initial render
  net_render_active_tab(app);

  // Main Loop
  while (!app->quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      net_handle_key(app, key);
      if (app->quit) break;
    }

    uint64_t now = net_get_ms();
    if (now - app->last_refresh_ms >= 1000ULL) {
      app->last_refresh_ms = now;
      bool r = get_hw_radio_power();
      if (r != app->radio_on) {
        app->radio_on = r;
        if (app->sw_radio) {
          if (app->radio_on) {
            lv_obj_add_state(app->sw_radio, LV_STATE_CHECKED);
          } else {
            lv_obj_remove_state(app->sw_radio, LV_STATE_CHECKED);
          }
        }
      }
    }

    eos_lvgl_step(10);
  }

  // Disable promisc on exit if active
  if (app->promisc_on) {
    int fd = open("/dev/wifi0", O_RDWR);
    if (fd >= 0) {
      eos_wifi_promisc_cfg_t pcfg = {0};
      ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg);
      close(fd);
    }
  }

  free(app);
  s_app = NULL;
  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_net_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_net",
    .name = "Network Manager",
    .group = "Tools",
    .description = "Graphical Wi-Fi scanner, STA/AP configuration, and bridge manager",
    .entry_point = lvgl_net_main,
};
