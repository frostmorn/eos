#include "ecore/bin.h"
#include "ecore/lvgl.h"
#include "emisc/netmisc.h"
#include "ebin/lvgl_net/lvgl_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  net_logic_t logic;

  // Modal Dialog UI state
  lv_obj_t *dialog_overlay;

  // UI Widgets
  lv_obj_t *hdr;
  lv_obj_t *hdr_title;
  lv_obj_t *tab_bar;
  lv_obj_t *tab_label;
  lv_obj_t *content_cont;
  lv_obj_t *toast_bar;
  lv_obj_t *toast_lbl;

  // Status switches
  lv_obj_t *sw_radio;
  lv_obj_t *sw_ap;
  lv_obj_t *sw_dhcpc;
  lv_obj_t *sw_dhcps;
  lv_obj_t *sw_napt;
  lv_obj_t *sw_promisc;

  // Selectable items
  lv_obj_t *items[NET_MAX_ITEMS];
  net_item_action_t item_actions[NET_MAX_ITEMS];
  int item_args[NET_MAX_ITEMS];
} net_app_t;

// ---------------------------------------------------------------------------
// Toast Notification
// ---------------------------------------------------------------------------

static void update_toast_ui(net_app_t *app) {
  if (!app || !app->toast_bar || !app->toast_lbl) return;
  lv_label_set_text(app->toast_lbl, app->logic.toast_msg);
  uint32_t col = (app->logic.status_type == NET_STATUS_WARN) ? COL_AMBER :
                 (app->logic.status_type == NET_STATUS_ERR) ? COL_RED :
                 (app->logic.status_type == NET_STATUS_SUCCESS) ? COL_GREEN :
                 COL_ACCENT_SKY;
  lv_obj_set_style_bg_color(app->toast_bar, lv_color_hex(col), 0);
  lv_obj_clear_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);
}

static void update_toast(net_app_t *app) {
  if (!app || !app->toast_bar) return;
  if (net_logic_update_toast(&app->logic)) {
    lv_obj_add_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------------------
// Forward Declarations
// ---------------------------------------------------------------------------

static void net_render_active_tab(net_app_t *app);
static void net_update_selection_highlight(net_app_t *app);
static void handle_item_action(net_app_t *app, int idx);
static void net_open_dialog(net_app_t *app, net_dialog_type_t type, const char *title,
                           const char *prompt, const char *initial, bool is_pw);
static void net_dialog_close(net_app_t *app);

// ---------------------------------------------------------------------------
// UI Component Creation Helpers
// ---------------------------------------------------------------------------

static void style_switch(lv_obj_t *sw) {
  lv_obj_set_size(sw, NET_SWITCH_WIDTH, NET_SWITCH_HEIGHT);
  lv_obj_set_style_bg_color(sw, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_bg_color(sw, lv_color_hex(COL_GREEN), LV_STATE_CHECKED);
  lv_obj_set_style_pad_all(sw, 0, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
}

static void item_click_event_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app || app->logic.dialog_active) return;
  lv_obj_t *target = lv_event_get_current_target(e);

  for (int i = 0; i < app->logic.item_count; i++) {
    if (app->items[i] == target) {
      app->logic.sel_idx = i;
      net_update_selection_highlight(app);
      handle_item_action(app, i);
      break;
    }
  }
}

static lv_obj_t *add_switch_row(net_app_t *app, lv_obj_t *parent, const char *label_text,
                                lv_obj_t **out_sw, bool state, net_item_action_t action) {
  if (app->logic.item_count >= NET_MAX_ITEMS) return NULL;
  int idx = app->logic.item_count++;

  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), NET_ROW_HEIGHT);
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_BG_CARD), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_radius(row, 3, 0);
  lv_obj_set_style_pad_hor(row, 6, 0);
  lv_obj_set_style_pad_ver(row, 2, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, item_click_event_cb, LV_EVENT_CLICKED, app);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT_PRIMARY), 0);
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

  if (out_sw) *out_sw = sw;

  app->items[idx] = row;
  app->item_actions[idx] = action;
  app->item_args[idx] = 0;
  return row;
}

static lv_obj_t *add_button_row(net_app_t *app, lv_obj_t *parent, const char *text,
                                const char *badge, uint32_t bg_color,
                                net_item_action_t action, int arg) {
  if (app->logic.item_count >= NET_MAX_ITEMS) return NULL;
  int idx = app->logic.item_count++;

  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_set_size(btn, lv_pct(100), NET_BTN_HEIGHT);
  lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, 3, 0);
  lv_obj_set_style_pad_hor(btn, 6, 0);
  lv_obj_set_style_pad_ver(btn, 2, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn, item_click_event_cb, LV_EVENT_CLICKED, app);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT_PRIMARY), 0);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

  if (badge && badge[0]) {
    lv_obj_t *blbl = lv_label_create(btn);
    lv_label_set_text(blbl, badge);
    lv_obj_set_style_text_color(blbl, lv_color_hex(COL_TEXT_SECOND), 0);
    lv_obj_align(blbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(blbl, LV_OBJ_FLAG_CLICKABLE);
  }

  app->items[idx] = btn;
  app->item_actions[idx] = action;
  app->item_args[idx] = arg;
  return btn;
}

static lv_obj_t *add_card(net_app_t *app, lv_obj_t *parent, const char *title,
                          const char *desc, uint32_t title_color,
                          net_item_action_t action, int arg) {
  if (app->logic.item_count >= NET_MAX_ITEMS) return NULL;
  int idx = app->logic.item_count++;

  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, lv_color_hex(COL_BG_CARD), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 3, 0);
  lv_obj_set_style_pad_all(card, 4, 0);
  lv_obj_set_style_pad_row(card, 2, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, item_click_event_cb, LV_EVENT_CLICKED, app);

  if (title && title[0]) {
    lv_obj_t *tlbl = lv_label_create(card);
    lv_label_set_text(tlbl, title);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(title_color), 0);
    lv_obj_clear_flag(tlbl, LV_OBJ_FLAG_CLICKABLE);
  }

  if (desc && desc[0]) {
    lv_obj_t *dlbl = lv_label_create(card);
    lv_label_set_text(dlbl, desc);
    lv_obj_set_style_text_color(dlbl, lv_color_hex(COL_TEXT_SECOND), 0);
    lv_obj_clear_flag(dlbl, LV_OBJ_FLAG_CLICKABLE);
  }

  app->items[idx] = card;
  app->item_actions[idx] = action;
  app->item_args[idx] = arg;
  return card;
}

// ---------------------------------------------------------------------------
// Selection and Scrolling
// ---------------------------------------------------------------------------

static void net_update_selection_highlight(net_app_t *app) {
  if (!app) return;
  for (int i = 0; i < app->logic.item_count; i++) {
    lv_obj_t *item = app->items[i];
    if (!item) continue;
    if (i == app->logic.sel_idx) {
      lv_obj_set_style_border_color(item, lv_color_hex(COL_ACCENT_LTBLUE), 0);
      lv_obj_set_style_border_width(item, 2, 0);
      lv_obj_scroll_to_view(item, LV_ANIM_OFF);
    } else {
      lv_obj_set_style_border_color(item, lv_color_hex(COL_BORDER), 0);
      lv_obj_set_style_border_width(item, 1, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// Actions & Handlers
// ---------------------------------------------------------------------------

static void do_toggle_radio(net_app_t *app) {
  net_logic_toggle_radio(&app->logic);
  update_toast_ui(app);
  net_render_active_tab(app);
}

static void do_scan(net_app_t *app) {
  net_logic_set_status(&app->logic, NET_STATUS_INFO, "Scanning Wi-Fi networks...");
  update_toast_ui(app);
  net_render_active_tab(app);
  eos_lvgl_step(NET_SCAN_STEP_MS);

  net_logic_scan(&app->logic);
  update_toast_ui(app);
  net_render_active_tab(app);
}

static void do_connect_ap(net_app_t *app, const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return;
  net_logic_set_status(&app->logic, NET_STATUS_INFO, "Connecting to \"%.24s\"...", ssid);
  update_toast_ui(app);
  net_render_active_tab(app);
  eos_lvgl_step(NET_SCAN_STEP_MS);

  net_logic_connect_ap(&app->logic, ssid, pass);
  update_toast_ui(app);
  net_render_active_tab(app);
}

static void net_confirm_disconnect_cb(bool confirmed, void *user_data) {
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;
  app->logic.dialog_active = false;
  app->dialog_overlay = NULL;
  if (confirmed) {
    net_logic_disconnect(&app->logic);
    update_toast_ui(app);
    net_render_active_tab(app);
  }
}

static void do_disconnect(net_app_t *app) {
  if (!app || app->logic.dialog_active) return;
  app->logic.dialog_active = true;
  eos_lvgl_confirm_dialog_cfg_t cfg = {
      .title = "Disconnect Wi-Fi",
      .message = "Disconnect from the active Wi-Fi network?",
      .confirm_label = "Disconnect",
      .cancel_label = "Cancel",
      .dangerous = true,
      .cb = net_confirm_disconnect_cb,
      .user_data = app,
  };
  app->dialog_overlay = eos_lvgl_dialog_confirm(&cfg);
}

static void do_toggle_ap(net_app_t *app) {
  net_logic_toggle_ap(&app->logic);
  update_toast_ui(app);
  net_render_active_tab(app);
}

static void do_toggle_napt(net_app_t *app) {
  net_logic_toggle_napt(&app->logic);
  update_toast_ui(app);
  net_render_active_tab(app);
}

static void do_toggle_promisc(net_app_t *app) {
  net_logic_toggle_promisc(&app->logic);
  update_toast_ui(app);
  net_render_active_tab(app);
}

static bool handle_softap_action(net_app_t *app, net_item_action_t act) {
  switch (act) {
    case ITEM_ACTION_TOGGLE_AP:
      do_toggle_ap(app);
      return true;

    case ITEM_ACTION_TOGGLE_DHCPS:
      net_logic_toggle_dhcps(&app->logic);
      update_toast_ui(app);
      net_render_active_tab(app);
      return true;

    case ITEM_ACTION_EDIT_AP_SSID:
      net_open_dialog(app, DIALOG_AP_SSID, "SoftAP SSID",
                      "Enter Hotspot SSID:", app->logic.ap_cfg.ssid, false);
      return true;

    case ITEM_ACTION_EDIT_AP_PASS:
      net_open_dialog(app, DIALOG_AP_PASS, "SoftAP Password",
                      "Enter Password (>=8 chars, or empty for Open):", app->logic.ap_cfg.password, true);
      return true;

    case ITEM_ACTION_CYCLE_AP_CHAN:
      net_logic_cycle_ap_chan(&app->logic);
      update_toast_ui(app);
      net_render_active_tab(app);
      return true;

    case ITEM_ACTION_TOGGLE_AP_SECURITY:
      net_logic_toggle_ap_security(&app->logic);
      update_toast_ui(app);
      net_render_active_tab(app);
      return true;

    default:
      return false;
  }
}

static void handle_item_action(net_app_t *app, int idx) {
  if (idx < 0 || idx >= app->logic.item_count) return;

  net_item_action_t act = app->item_actions[idx];
  int arg = app->item_args[idx];

  if (handle_softap_action(app, act)) {
    return;
  }

  switch (act) {
    case ITEM_ACTION_TOGGLE_RADIO:
      do_toggle_radio(app);
      break;

    case ITEM_ACTION_SCAN:
      do_scan(app);
      break;

    case ITEM_ACTION_DISCONNECT:
      do_disconnect(app);
      break;

    case ITEM_ACTION_CONNECT_AP:
      if (arg >= 0 && arg < app->logic.ap_count) {
        if (app->logic.aps[arg].authmode > 0) {
          app->logic.dialog_ap_idx = arg;
          char pbuf[EOS_HEADER_STR_LEN];
          snprintf(pbuf, sizeof(pbuf), "Enter password for \"%.24s\":", app->logic.aps[arg].ssid);
          net_open_dialog(app, DIALOG_CONNECT_AP_PW, "Connect to Wi-Fi", pbuf, "", true);
        } else {
          do_connect_ap(app, app->logic.aps[arg].ssid, "");
        }
      }
      break;

    case ITEM_ACTION_CONNECT_MANUAL:
      net_open_dialog(app, DIALOG_MANUAL_SSID, "Connect to Wi-Fi",
                      "Enter Network SSID:", "", false);
      break;

    case ITEM_ACTION_TOGGLE_DHCPC:
      net_logic_toggle_dhcpc(&app->logic);
      update_toast_ui(app);
      net_render_active_tab(app);
      break;

    case ITEM_ACTION_EDIT_STATIC_IP:
      net_open_dialog(app, DIALOG_STA_STATIC_IP, "Set Static IP",
                      "IP [Mask GW] (e.g. 192.168.1.150):", "192.168.1.150", false);
      break;

    case ITEM_ACTION_TOGGLE_NAPT:
      do_toggle_napt(app);
      break;

    case ITEM_ACTION_TOGGLE_IFACE:
      if (arg >= 0 && arg < NET_MAX_IFACES) {
        net_logic_toggle_iface(arg);
        net_render_active_tab(app);
      }
      break;

    case ITEM_ACTION_TOGGLE_PROMISC:
      do_toggle_promisc(app);
      break;

    case ITEM_ACTION_NONE:
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Modal Dialogs
// ---------------------------------------------------------------------------

static void net_dialog_close(net_app_t *app) {
  if (app && app->dialog_overlay) {
    eos_lvgl_dialog_close(app->dialog_overlay);
    app->dialog_overlay = NULL;
  }
  if (app) {
    app->logic.dialog_active = false;
    app->logic.dialog_type = DIALOG_NONE;
  }
}

static void net_dialog_cancel_cb(const char *unused, void *user_data) {
  (void)unused;
  net_app_t *app = (net_app_t *)user_data;
  if (app) {
    app->logic.dialog_active = false;
    app->logic.dialog_type = DIALOG_NONE;
    app->dialog_overlay = NULL;
  }
}

static void net_dialog_submit_cb(const char *text, void *user_data) {
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;
  net_dialog_type_t dtype = app->logic.dialog_type;
  int idx = app->logic.dialog_ap_idx;
  app->logic.dialog_active = false;
  app->logic.dialog_type = DIALOG_NONE;
  app->dialog_overlay = NULL;

  switch (dtype) {
    case DIALOG_CONNECT_AP_PW:
      if (idx >= 0 && idx < app->logic.ap_count) {
        do_connect_ap(app, app->logic.aps[idx].ssid, text ? text : "");
      }
      break;

    case DIALOG_MANUAL_SSID:
      if (text && text[0]) {
        strlcpy(app->logic.dialog_buf, text, sizeof(app->logic.dialog_buf));
        net_open_dialog(app, DIALOG_MANUAL_PW, "Wi-Fi Password",
                        "Enter password (empty for Open network):", "", true);
      }
      break;

    case DIALOG_MANUAL_PW:
      if (app->logic.dialog_buf[0]) {
        do_connect_ap(app, app->logic.dialog_buf, text ? text : "");
        app->logic.dialog_buf[0] = '\0';
      }
      break;

    case DIALOG_AP_SSID:
      if (text && text[0]) {
        net_logic_set_ap_ssid(&app->logic, text);
        update_toast_ui(app);
        net_render_active_tab(app);
      }
      break;

    case DIALOG_AP_PASS:
      net_logic_set_ap_pass(&app->logic, text);
      update_toast_ui(app);
      net_render_active_tab(app);
      break;

    case DIALOG_STA_STATIC_IP:
      if (text && text[0]) {
        net_logic_set_sta_static_ip(&app->logic, text);
        update_toast_ui(app);
        net_render_active_tab(app);
      }
      break;

    default:
      break;
  }
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

// ---------------------------------------------------------------------------
// Tab Content Rendering
// ---------------------------------------------------------------------------

static const char *tab_titles[NET_NUM_TABS] = {
    "Wi-Fi Station",
    "SoftAP Hotspot",
    "Network Bridge",
    "All Interfaces",
    "802.11 Sniffer",
};

static void net_render_tab_sta(net_app_t *app, bool has_sta,
                               const eos_netif_info_t *sta_info) {
  add_switch_row(app, app->content_cont, "Wi-Fi Radio Power", &app->sw_radio,
                 app->logic.radio_on, ITEM_ACTION_TOGGLE_RADIO);

  add_switch_row(app, app->content_cont, "DHCP Client (sta0)", &app->sw_dhcpc,
                 app->logic.sta_dhcp_on, ITEM_ACTION_TOGGLE_DHCPC);

  eos_sta_ap_info_t sta_ap;
  net_logic_ioctl_get_sta_ap_info(&sta_ap);

  char title[EOS_LINE_STR_LEN];
  char desc[EOS_MID_STR_LEN];
  uint32_t tcol = COL_TEXT_SECOND;

  if (app->logic.sta_connected && sta_ap.ssid[0]) {
    snprintf(title, sizeof(title), "Connected: %s", sta_ap.ssid);
    tcol = COL_GREEN;
    snprintf(desc, sizeof(desc),
             "Signal: %d dBm (Ch %u)\n"
             "IP: %s  Mask: %s\n"
             "GW: %s\n"
             "Mode: %s",
             (int)sta_ap.rssi, (unsigned)sta_ap.channel,
             sta_info->ip.ip[0] ? sta_info->ip.ip : "0.0.0.0",
             sta_info->ip.netmask[0] ? sta_info->ip.netmask : "0.0.0.0",
             sta_info->ip.gw[0] ? sta_info->ip.gw : "0.0.0.0",
             app->logic.sta_dhcp_on ? "DHCP Auto-Assigned" : "Static IP");
  } else if (has_sta && (sta_info->flags & EOS_NETIF_FLAG_UP)) {
    snprintf(title, sizeof(title), "Station Interface UP (Idle)");
    tcol = COL_AMBER;
    snprintf(desc, sizeof(desc),
             "Ready to connect. MAC: %02x:%02x:%02x:%02x:%02x:%02x\n"
             "DHCP Client: %s",
             sta_info->mac[0], sta_info->mac[1], sta_info->mac[2],
             sta_info->mac[3], sta_info->mac[4], sta_info->mac[5],
             app->logic.sta_dhcp_on ? "Enabled" : "Disabled (Static)");
  } else {
    snprintf(title, sizeof(title), "Station Interface DOWN");
    tcol = COL_TEXT_SECOND;
    snprintf(desc, sizeof(desc), "Turn radio ON or scan to connect.");
  }

  add_card(app, app->content_cont, title, desc, tcol, ITEM_ACTION_NONE, 0);

  add_button_row(app, app->content_cont, "[ Scan Wi-Fi Networks ]", "", COL_ACCENT_BLUE,
                 ITEM_ACTION_SCAN, 0);
  add_button_row(app, app->content_cont, "[ Connect to Hidden / Manual SSID ]", "", COL_BG_CARD,
                 ITEM_ACTION_CONNECT_MANUAL, 0);

  if (!app->logic.sta_dhcp_on) {
    char ip_badge[EOS_SMALL_STR_LEN];
    snprintf(ip_badge, sizeof(ip_badge), "%s [Edit]",
             sta_info->ip.ip[0] ? sta_info->ip.ip : "192.168.1.150");
    add_button_row(app, app->content_cont, "Static IP Config:", ip_badge, COL_BG_CARD,
                   ITEM_ACTION_EDIT_STATIC_IP, 0);
  }

  if (app->logic.sta_connected) {
    add_button_row(app, app->content_cont, "[ Disconnect ]", "", COL_RED_DARK,
                   ITEM_ACTION_DISCONNECT, 0);
  }

  if (app->logic.ap_count > 0) {
    lv_obj_t *lhdr = lv_label_create(app->content_cont);
    lv_label_set_text(lhdr, "Available Networks:");
    lv_obj_set_style_text_color(lhdr, lv_color_hex(COL_ACCENT_SKY), 0);

    for (int i = 0; i < app->logic.ap_count; i++) {
      char badge[EOS_SMALL_STR_LEN];
      const char *sec = eos_net_wifi_auth_name(app->logic.aps[i].authmode);
      snprintf(badge, sizeof(badge), "%d dBm Ch%u %s",
               (int)app->logic.aps[i].rssi, (unsigned)app->logic.aps[i].channel, sec);
      add_button_row(app, app->content_cont, app->logic.aps[i].ssid, badge, COL_BG_CARD,
                     ITEM_ACTION_CONNECT_AP, i);
    }
  }
}

static void net_render_tab_ap(net_app_t *app, const eos_netif_info_t *ap_info) {
  eos_sta_ap_info_t sta_ap;
  memset(&sta_ap, 0, sizeof(sta_ap));
  net_logic_ioctl_get_sta_ap_info(&sta_ap);

  if (app->logic.sta_connected && sta_ap.channel > 0) {
    char notice_desc[EOS_MID_STR_LEN];
    snprintf(notice_desc, sizeof(notice_desc),
             "Single PHY radio shared in concurrent AP+STA mode.\n"
             "SoftAP channel is locked to Station channel: Ch %u (%u MHz).\n"
             "(Configured SoftAP channel: Ch %u)",
             (unsigned)sta_ap.channel,
             (unsigned)(NET_WIFI_BASE_FREQ_MHZ + sta_ap.channel * NET_WIFI_CHAN_SPACING_MHZ),
             (unsigned)app->logic.ap_cfg.channel);
    add_card(app, app->content_cont, "! AP-STA Shared Channel Active", notice_desc,
             COL_AMBER, ITEM_ACTION_NONE, 0);
  } else {
    add_card(app, app->content_cont, "i AP-STA Shared Channel Rule",
             "ESP32 shares 1 RF transceiver. When Station connects\n"
             "to a router, SoftAP automatically shifts to the\n"
             "router's channel to maintain concurrent link.",
             COL_ACCENT_SKY, ITEM_ACTION_NONE, 0);
  }

  add_switch_row(app, app->content_cont, "SoftAP Power", &app->sw_ap,
                 app->logic.ap_on, ITEM_ACTION_TOGGLE_AP);

  add_switch_row(app, app->content_cont, "DHCP Server (ap0)", &app->sw_dhcps,
                 app->logic.ap_dhcp_on, ITEM_ACTION_TOGGLE_DHCPS);

  char ssid_badge[EOS_BADGE_STR_LEN];
  snprintf(ssid_badge, sizeof(ssid_badge), "\"%s\" [Edit]", app->logic.ap_cfg.ssid);
  add_button_row(app, app->content_cont, "SoftAP SSID:", ssid_badge, COL_BG_CARD,
                 ITEM_ACTION_EDIT_AP_SSID, 0);

  char sec_title[EOS_SMALL_STR_LEN];
  char sec_badge[EOS_BADGE_STR_LEN];
  const char *auth_str = (app->logic.ap_cfg.authmode == 0) ? "Open" : "WPA2-PSK";
  snprintf(sec_title, sizeof(sec_title), "Security: %s", auth_str);
  snprintf(sec_badge, sizeof(sec_badge), "%s [Toggle]",
           (app->logic.ap_cfg.authmode == 0) ? "No Pass" : "Encrypted");
  add_button_row(app, app->content_cont, sec_title, sec_badge, COL_BG_CARD,
                 ITEM_ACTION_TOGGLE_AP_SECURITY, 0);

  if (app->logic.ap_cfg.authmode != 0) {
    char pw_masked[EOS_SMALL_STR_LEN];
    size_t pw_len = strlen(app->logic.ap_cfg.password);
    if (pw_len > 0) {
      memset(pw_masked, '*', sizeof(pw_masked));
      if (pw_len > 10) pw_len = 10;
      pw_masked[pw_len] = '\0';
    } else {
      strlcpy(pw_masked, "(none)", sizeof(pw_masked));
    }
    char pw_badge[EOS_BADGE_STR_LEN];
    snprintf(pw_badge, sizeof(pw_badge), "%s [Change]", pw_masked);
    add_button_row(app, app->content_cont, "Password:", pw_badge, COL_BG_CARD,
                   ITEM_ACTION_EDIT_AP_PASS, 0);
  }

  char chan_title[EOS_BADGE_STR_LEN];
  char chan_badge[EOS_BADGE_STR_LEN];
  uint8_t eff_chan = (app->logic.sta_connected && sta_ap.channel > 0) ? sta_ap.channel : app->logic.ap_cfg.channel;
  snprintf(chan_title, sizeof(chan_title), "Channel: Ch %u (%u MHz)",
           (unsigned)eff_chan,
           (unsigned)(NET_WIFI_BASE_FREQ_MHZ + eff_chan * NET_WIFI_CHAN_SPACING_MHZ));
  snprintf(chan_badge, sizeof(chan_badge), "[Cycle 1-13]");
  add_button_row(app, app->content_cont, chan_title, chan_badge, COL_BG_CARD,
                 ITEM_ACTION_CYCLE_AP_CHAN, 0);

  char ap_desc[EOS_MID_STR_LEN];
  if (app->logic.ap_on) {
    snprintf(ap_desc, sizeof(ap_desc),
             "SSID: %s  Ch: %u\n"
             "IP: %s  Mask: %s\n"
             "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n"
             "DHCP Server: %s",
             app->logic.ap_cfg.ssid, (unsigned)eff_chan,
             ap_info->ip.ip[0] ? ap_info->ip.ip : "192.168.4.1",
             ap_info->ip.netmask[0] ? ap_info->ip.netmask : "255.255.255.0",
             ap_info->mac[0], ap_info->mac[1], ap_info->mac[2],
             ap_info->mac[3], ap_info->mac[4], ap_info->mac[5],
             app->logic.ap_dhcp_on ? "Enabled" : "Disabled");
    add_card(app, app->content_cont, "SoftAP Active (UP)", ap_desc, COL_GREEN, ITEM_ACTION_NONE, 0);

    eos_ap_sta_list_t stas;
    net_logic_get_ap_stas(&stas);
    char c_desc[EOS_BUFFER_128_LEN];
    snprintf(c_desc, sizeof(c_desc), "Connected stations: %u / 4", (unsigned)stas.count);
    add_card(app, app->content_cont, "Hotspot Clients", c_desc, COL_ACCENT_SKY, ITEM_ACTION_NONE, 0);

    add_button_row(app, app->content_cont, "[ Stop Hotspot ]", "", COL_RED_DARK,
                   ITEM_ACTION_TOGGLE_AP, 0);
  } else {
    snprintf(ap_desc, sizeof(ap_desc),
             "Hotspot is disabled.\n"
             "Configured SSID: \"%s\"\n"
             "Channel: %u  Security: %s\n"
             "Default IP: 192.168.4.1",
             app->logic.ap_cfg.ssid, (unsigned)app->logic.ap_cfg.channel, auth_str);
    add_card(app, app->content_cont, "SoftAP Inactive", ap_desc, COL_TEXT_SECOND, ITEM_ACTION_NONE, 0);

    add_button_row(app, app->content_cont, "[ Start Hotspot ]", "", COL_ACCENT_BLUE,
                   ITEM_ACTION_TOGGLE_AP, 0);
  }
}

static void net_render_tab_bridge(net_app_t *app, bool has_br,
                                  const eos_netif_bridge_info_t *br_info) {
  add_switch_row(app, app->content_cont, "Bridge NAT (NAPT)", &app->sw_napt,
                 app->logic.napt_on, ITEM_ACTION_TOGGLE_NAPT);

  char bdesc[EOS_MID_STR_LEN];
  if (has_br) {
    char ports_str[EOS_BUFFER_128_LEN] = {0};
    for (int i = 0; i < br_info->port_count; i++) {
      if (i > 0) strlcat(ports_str, ", ", sizeof(ports_str));
      strlcat(ports_str, br_info->ports[i], sizeof(ports_str));
    }
    snprintf(bdesc, sizeof(bdesc),
             "Bridge: %s\n"
             "Ports (%u): %s\n"
             "NAPT: %s",
             br_info->name[0] ? br_info->name : "br0",
             (unsigned)br_info->port_count,
             ports_str[0] ? ports_str : "(none)",
             br_info->napt_enabled ? "Active" : "Disabled");
    add_card(app, app->content_cont, "Virtual Bridge Active", bdesc, COL_GREEN, ITEM_ACTION_NONE, 0);
  } else {
    snprintf(bdesc, sizeof(bdesc),
             "Bridge br0 is not attached.\n"
             "Attach via: devctl attach net bridge");
    add_card(app, app->content_cont, "Bridge Inactive", bdesc, COL_TEXT_SECOND, ITEM_ACTION_NONE, 0);
  }
}

static void net_render_tab_ifaces(net_app_t *app) {
  eos_netif_info_t w_info;
  bool has_w = net_logic_ioctl_get_netif_info("wifi0", &w_info);

  char hdesc[EOS_BUFFER_128_LEN];
  snprintf(hdesc, sizeof(hdesc),
           "Controller: %s\n"
           "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
           has_w ? (w_info.flags & EOS_NETIF_FLAG_UP ? "Radio UP" : "Radio DOWN") : "Unknown",
           w_info.mac[0], w_info.mac[1], w_info.mac[2],
           w_info.mac[3], w_info.mac[4], w_info.mac[5]);
  add_card(app, app->content_cont, "WLAN PHY Controller (wifi0)", hdesc, COL_ACCENT_SKY,
           ITEM_ACTION_TOGGLE_RADIO, 0);

  const char *devs[NET_MAX_IFACES] = {"wifi0", "sta0", "ap0", "br0"};
  for (int d = 0; d < NET_MAX_IFACES; d++) {
    eos_netif_info_t inf;
    if (!net_logic_ioctl_get_netif_info(devs[d], &inf)) continue;

    bool is_up = (inf.flags & EOS_NETIF_FLAG_UP) != 0;
    bool is_link = (inf.flags & EOS_NETIF_FLAG_LINK_UP) != 0;
    bool has_dhcpc = (inf.flags & EOS_NETIF_FLAG_DHCP_CLIENT) != 0;
    bool has_dhcps = (inf.flags & EOS_NETIF_FLAG_DHCP_SERVER) != 0;

    char card_title[EOS_LINE_STR_LEN];
    snprintf(card_title, sizeof(card_title), "%s  [%s%s]", devs[d],
             is_up ? "UP" : "DOWN",
             is_link ? ", LINK" : "");

    char card_desc[EOS_MID_STR_LEN];
    snprintf(card_desc, sizeof(card_desc),
             "IP: %s  Mask: %s\n"
             "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n"
             "DHCP: %s\n"
             "[A: Toggle UP/DOWN]",
             inf.ip.ip[0] ? inf.ip.ip : "0.0.0.0",
             inf.ip.netmask[0] ? inf.ip.netmask : "0.0.0.0",
             inf.mac[0], inf.mac[1], inf.mac[2],
             inf.mac[3], inf.mac[4], inf.mac[5],
             has_dhcpc ? "Client (Active)" : (has_dhcps ? "Server (Active)" : "Off/Static"));

    add_card(app, app->content_cont, card_title, card_desc,
             is_up ? COL_GREEN : COL_TEXT_SECOND,
             ITEM_ACTION_TOGGLE_IFACE, d);
  }
}

static void net_render_tab_sniffer(net_app_t *app) {
  add_switch_row(app, app->content_cont, "802.11 Monitor Mode", &app->sw_promisc,
                 app->logic.promisc_on, ITEM_ACTION_TOGGLE_PROMISC);

  char sdesc[EOS_MID_STR_LEN];
  snprintf(sdesc, sizeof(sdesc),
           "Promiscuous mode captures raw 802.11 MAC\n"
           "frames from /dev/wifi0 via read().\n"
           "Status: %s",
           app->logic.promisc_on ? "MONITORING ACTIVE" : "Inactive");
  add_card(app, app->content_cont, "Raw Packet Capture", sdesc,
           app->logic.promisc_on ? COL_GREEN : COL_TEXT_SECOND,
           ITEM_ACTION_TOGGLE_PROMISC, 0);

  if (app->logic.promisc_on) {
    add_card(app, app->content_cont, "! Conflict Warning",
             "Monitor mode interrupts normal Wi-Fi association.",
             COL_AMBER, ITEM_ACTION_NONE, 0);
  }
}

static void net_render_active_tab(net_app_t *app) {
  if (!app || !app->content_cont) return;

  int32_t saved_scroll_y = lv_obj_get_scroll_y(app->content_cont);

  lv_obj_clean(app->content_cont);
  memset(app->items, 0, sizeof(app->items));
  memset(app->item_actions, 0, sizeof(app->item_actions));
  memset(app->item_args, 0, sizeof(app->item_args));
  app->logic.item_count = 0;
  app->sw_radio = NULL;
  app->sw_ap = NULL;
  app->sw_dhcpc = NULL;
  app->sw_dhcps = NULL;
  app->sw_napt = NULL;
  app->sw_promisc = NULL;

  net_logic_refresh_state(&app->logic);

  eos_netif_info_t sta_info;
  bool has_sta = net_logic_ioctl_get_netif_info("sta0", &sta_info);

  eos_netif_info_t ap_info;
  net_logic_ioctl_get_netif_info("ap0", &ap_info);

  eos_netif_bridge_info_t br_info;
  bool has_br = net_logic_ioctl_get_bridge_info(&br_info);

  char tbar_str[EOS_LINE_STR_LEN];
  snprintf(tbar_str, sizeof(tbar_str), "<  %s (%d/%d)  >",
           tab_titles[app->logic.cur_tab], app->logic.cur_tab + 1, NET_NUM_TABS);
  lv_label_set_text(app->tab_label, tbar_str);

  eos_toolbar_set(tab_titles[app->logic.cur_tab], "[▲▼]Select  [◄►]Tab  [A]Action  [B]Exit");

  switch (app->logic.cur_tab) {
    case TAB_STA:
      net_render_tab_sta(app, has_sta, &sta_info);
      break;
    case TAB_AP:
      net_render_tab_ap(app, &ap_info);
      break;
    case TAB_BRIDGE:
      net_render_tab_bridge(app, has_br, &br_info);
      break;
    case TAB_IFACES:
      net_render_tab_ifaces(app);
      break;
    case TAB_SNIFFER:
      net_render_tab_sniffer(app);
      break;
    default:
      break;
  }

  if (app->logic.sel_idx >= app->logic.item_count) {
    app->logic.sel_idx = app->logic.item_count > 0 ? app->logic.item_count - 1 : 0;
  }
  if (saved_scroll_y > 0) {
    lv_obj_scroll_to_y(app->content_cont, saved_scroll_y, LV_ANIM_OFF);
  }
  net_update_selection_highlight(app);
}

static void net_gesture_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app || app->logic.dialog_active) return;
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_LEFT) {
    app->logic.cur_tab = (app->logic.cur_tab + 1) % NET_NUM_TABS;
    app->logic.sel_idx = 0;
    net_render_active_tab(app);
  } else if (dir == LV_DIR_RIGHT) {
    app->logic.cur_tab = (app->logic.cur_tab - 1 + NET_NUM_TABS) % NET_NUM_TABS;
    app->logic.sel_idx = 0;
    net_render_active_tab(app);
  }
}

// ---------------------------------------------------------------------------
// Hardware Key Handler
// ---------------------------------------------------------------------------

static void net_handle_key(net_app_t *app, uint16_t key) {
  if (app->logic.dialog_active) {
    if (key == KEY_ESC) {
      net_dialog_close(app);
    }
    return;
  }

  switch (key) {
    case KEY_UP:
      if (app->logic.item_count > 0) {
        app->logic.sel_idx = (app->logic.sel_idx - 1 + app->logic.item_count) % app->logic.item_count;
        net_update_selection_highlight(app);
      } else if (app->content_cont) {
        lv_obj_scroll_by(app->content_cont, 0, NET_SCROLL_STEP, LV_ANIM_OFF);
      }
      break;

    case KEY_DOWN:
      if (app->logic.item_count > 0) {
        app->logic.sel_idx = (app->logic.sel_idx + 1) % app->logic.item_count;
        net_update_selection_highlight(app);
      } else if (app->content_cont) {
        lv_obj_scroll_by(app->content_cont, 0, -NET_SCROLL_STEP, LV_ANIM_OFF);
      }
      break;

    case KEY_LEFT:
    case KEY_C:
      app->logic.cur_tab = (app->logic.cur_tab - 1 + NET_NUM_TABS) % NET_NUM_TABS;
      app->logic.sel_idx = 0;
      net_render_active_tab(app);
      break;

    case KEY_RIGHT:
    case KEY_D:
      app->logic.cur_tab = (app->logic.cur_tab + 1) % NET_NUM_TABS;
      app->logic.sel_idx = 0;
      net_render_active_tab(app);
      break;

    case KEY_ENTER:
    case KEY_A:
      if (app->logic.item_count > 0) {
        handle_item_action(app, app->logic.sel_idx);
      }
      break;

    case KEY_ESC:
    case KEY_B:
    case KEY_BACKSPACE:
      app->logic.quit = true;
      break;

    default:
      break;
  }
}

static void net_back_btn_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (app) {
    app->logic.quit = true;
  }
}

static void tab_bar_click_event_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app || app->logic.dialog_active) return;
  app->logic.cur_tab = (app->logic.cur_tab + 1) % NET_NUM_TABS;
  app->logic.sel_idx = 0;
  net_render_active_tab(app);
}

// ---------------------------------------------------------------------------
// Main Application Entry
// ---------------------------------------------------------------------------

static void net_setup_ui(net_app_t *app, lv_obj_t *scr, uint32_t w, uint32_t h) {
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG_DEEP), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Top Header Bar
  app->hdr = lv_obj_create(scr);
  lv_obj_set_size(app->hdr, (int32_t)w, NET_HDR_HEIGHT);
  lv_obj_align(app->hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(app->hdr, lv_color_hex(COL_HDR_BG), 0);
  lv_obj_set_style_bg_opa(app->hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->hdr, 0, 0);
  lv_obj_set_style_radius(app->hdr, 0, 0);
  lv_obj_set_style_pad_hor(app->hdr, 6, 0);
  lv_obj_set_style_pad_ver(app->hdr, 1, 0);
  lv_obj_clear_flag(app->hdr, LV_OBJ_FLAG_SCROLLABLE);

  app->hdr_title = lv_label_create(app->hdr);
  lv_label_set_text(app->hdr_title, "EOS Network");
  lv_obj_set_style_text_color(app->hdr_title, lv_color_hex(COL_WHITE), 0);
  lv_obj_align(app->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *back_btn = eos_lvgl_create_back_btn(app->hdr, net_back_btn_cb, app);
  if (back_btn) {
    lv_obj_set_size(back_btn, 24, 16);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, -2, 0);
  }

  // 2. Tab Selector Bar
  int32_t tab_y = NET_HDR_HEIGHT;
  int32_t tab_h = NET_TAB_HEIGHT;
  app->tab_bar = lv_obj_create(scr);
  lv_obj_set_size(app->tab_bar, (int32_t)w, tab_h);
  lv_obj_align(app->tab_bar, LV_ALIGN_TOP_MID, 0, tab_y);
  lv_obj_set_style_bg_color(app->tab_bar, lv_color_hex(COL_TAB_BG), 0);
  lv_obj_set_style_bg_opa(app->tab_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->tab_bar, 0, 0);
  lv_obj_set_style_radius(app->tab_bar, 0, 0);
  lv_obj_set_style_pad_all(app->tab_bar, 1, 0);
  lv_obj_clear_flag(app->tab_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(app->tab_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(app->tab_bar, tab_bar_click_event_cb, LV_EVENT_CLICKED, app);

  app->tab_label = lv_label_create(app->tab_bar);
  lv_obj_set_style_text_color(app->tab_label, lv_color_hex(COL_YELLOW), 0);
  lv_obj_center(app->tab_label);
  lv_obj_clear_flag(app->tab_label, LV_OBJ_FLAG_CLICKABLE);

  // 3. Toast Notification Bar
  app->toast_bar = lv_obj_create(scr);
  lv_obj_set_size(app->toast_bar, (int32_t)w - 4, NET_TOAST_HEIGHT);
  lv_obj_align(app->toast_bar, LV_ALIGN_TOP_MID, 0, tab_y + tab_h + 1);
  lv_obj_set_style_bg_color(app->toast_bar, lv_color_hex(COL_AMBER), 0);
  lv_obj_set_style_bg_opa(app->toast_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->toast_bar, 0, 0);
  lv_obj_set_style_radius(app->toast_bar, 3, 0);
  lv_obj_set_style_pad_all(app->toast_bar, 1, 0);
  lv_obj_clear_flag(app->toast_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);

  app->toast_lbl = lv_label_create(app->toast_bar);
  lv_obj_set_style_text_color(app->toast_lbl, lv_color_hex(COL_BG_DEEP), 0);
  lv_obj_align(app->toast_lbl, LV_ALIGN_LEFT_MID, 2, 0);

  // 4. Main Scrollable Content Container
  int32_t content_y = tab_y + tab_h + 1;
  int32_t content_h = (int32_t)h - content_y - 2;
  if (content_h < NET_MIN_CONTENT_H) content_h = NET_MIN_CONTENT_H;

  app->content_cont = lv_obj_create(scr);
  lv_obj_set_size(app->content_cont, (int32_t)w - 4, content_h);
  lv_obj_align(app->content_cont, LV_ALIGN_TOP_MID, 0, content_y);
  lv_obj_set_style_bg_color(app->content_cont, lv_color_hex(COL_BG_PANEL), 0);
  lv_obj_set_style_bg_opa(app->content_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(app->content_cont, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(app->content_cont, 1, 0);
  lv_obj_set_style_radius(app->content_cont, 3, 0);
  lv_obj_set_style_pad_all(app->content_cont, 3, 0);
  lv_obj_set_style_pad_row(app->content_cont, 3, 0);
  lv_obj_set_flex_flow(app->content_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(app->content_cont, LV_DIR_VER);
  lv_obj_clear_flag(app->content_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_flag(app->content_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_add_flag(app->content_cont, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(scr, net_gesture_cb, LV_EVENT_GESTURE, app);
  lv_obj_add_event_cb(app->content_cont, net_gesture_cb, LV_EVENT_GESTURE, app);
}

static void net_event_loop(net_app_t *app) {
  while (!app->logic.quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      net_handle_key(app, key);
      if (app->logic.quit) break;
    }

    uint64_t now = net_logic_get_ms();
    if (now - app->logic.last_refresh_ms >= NET_REFRESH_INTERVAL_MS) {
      app->logic.last_refresh_ms = now;
      bool r = net_logic_ioctl_get_radio_power();
      if (r != app->logic.radio_on) {
        app->logic.radio_on = r;
        if (app->sw_radio) {
          if (app->logic.radio_on) {
            lv_obj_add_state(app->sw_radio, LV_STATE_CHECKED);
          } else {
            lv_obj_remove_state(app->sw_radio, LV_STATE_CHECKED);
          }
        }
      }
    }

    update_toast(app);
    eos_lvgl_step(NET_STEP_DELAY_MS);
  }
}

int lvgl_net_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_net: failed to init LVGL\n");
    return -1;
  }

  net_app_t *app = calloc(1, sizeof(net_app_t));
  if (!app) {
    eos_lvgl_deinit();
    return -1;
  }

  net_logic_init(&app->logic);

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();
  lv_obj_t *scr = lv_screen_active();

  net_setup_ui(app, scr, w, h);
  net_render_active_tab(app);
  net_event_loop(app);

  if (app->logic.promisc_on) {
    net_logic_ioctl_set_promisc(false);
  }

  eos_toolbar_clear();
  free(app);
  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_net_app = {
    .magic = EOS_BIN_MAGIC,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_GUI_STACK_SIZE,
    .filename = "lvgl_net",
    .name = "Network Manager",
    .group = "Tools",
    .description = "Graphical Wi-Fi scanner, STA/AP configuration, and bridge manager",
    .entry_point = lvgl_net_main,
};
