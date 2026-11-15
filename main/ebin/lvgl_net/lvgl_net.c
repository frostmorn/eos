#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/lvgl.h"
#include "ecore/netif.h"
#include "emisc/netmisc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#define NET_MAX_SCAN_APS 16
#define NET_NUM_TABS     5
#define NET_MAX_ITEMS    48

// ---------------------------------------------------------------------------
// Color Palette (standard EOS theme from ecore/lvgl.h)
// ---------------------------------------------------------------------------
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

typedef enum {
  TAB_STA = 0,
  TAB_AP,
  TAB_BRIDGE,
  TAB_IFACES,
  TAB_SNIFFER,
} net_tab_t;

typedef enum {
  ITEM_ACTION_NONE = 0,
  ITEM_ACTION_TOGGLE_RADIO,
  ITEM_ACTION_SCAN,
  ITEM_ACTION_DISCONNECT,
  ITEM_ACTION_CONNECT_AP,
  ITEM_ACTION_CONNECT_MANUAL,
  ITEM_ACTION_TOGGLE_DHCPC,
  ITEM_ACTION_EDIT_STATIC_IP,
  ITEM_ACTION_TOGGLE_AP,
  ITEM_ACTION_TOGGLE_DHCPS,
  ITEM_ACTION_EDIT_AP_SSID,
  ITEM_ACTION_EDIT_AP_PASS,
  ITEM_ACTION_CYCLE_AP_CHAN,
  ITEM_ACTION_TOGGLE_AP_SECURITY,
  ITEM_ACTION_TOGGLE_NAPT,
  ITEM_ACTION_TOGGLE_IFACE,
  ITEM_ACTION_TOGGLE_PROMISC,
} net_item_action_t;

typedef enum {
  DIALOG_NONE = 0,
  DIALOG_CONNECT_AP_PW,
  DIALOG_MANUAL_SSID,
  DIALOG_MANUAL_PW,
  DIALOG_AP_SSID,
  DIALOG_AP_PASS,
  DIALOG_STA_STATIC_IP,
} net_dialog_type_t;

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

  // Cached hardware state
  bool radio_on;
  bool ap_on;
  bool napt_on;
  bool promisc_on;
  bool sta_connected;
  bool sta_dhcp_on;
  bool ap_dhcp_on;

  // SoftAP Configuration
  eos_ap_config_t ap_cfg;

  // Scan & Connection
  net_ap_item_t aps[NET_MAX_SCAN_APS];
  int ap_count;
  char connect_ssid[33];
  char status_msg[128];
  uint8_t status_type; // 0=info, 1=warning, 2=error, 3=success

  // Modal Dialog state
  bool dialog_active;
  net_dialog_type_t dialog_type;
  int dialog_ap_idx;
  char dialog_buf[64];
  lv_obj_t *dialog_overlay;

  // Toast notification state
  char toast_msg[128];
  uint64_t toast_expire_ms;

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

  uint64_t last_refresh_ms;
} net_app_t;

static uint64_t net_get_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static int open_iface_dev(const char *dev) {
  if (!dev || !dev[0]) return -1;
  char path[64];
  if (dev[0] == '/') {
    snprintf(path, sizeof(path), "%s", dev);
  } else {
    snprintf(path, sizeof(path), "/dev/%s", dev);
  }
  return open(path, O_RDWR);
}

// ---------------------------------------------------------------------------
// Toast Notification
// ---------------------------------------------------------------------------

static void set_status(net_app_t *app, uint8_t type, const char *fmt, ...) {
  if (!app) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(app->status_msg, sizeof(app->status_msg), fmt, ap);
  va_end(ap);
  app->status_type = type;

  snprintf(app->toast_msg, sizeof(app->toast_msg), "%s", app->status_msg);
  app->toast_expire_ms = net_get_ms() + 3500ULL;

  if (app->toast_bar && app->toast_lbl) {
    lv_label_set_text(app->toast_lbl, app->toast_msg);
    uint32_t col = (type == 1) ? COL_AMBER :
                   (type == 2) ? COL_RED :
                   (type == 3) ? COL_GREEN :
                   COL_ACCENT_SKY;
    lv_obj_set_style_bg_color(app->toast_bar, lv_color_hex(col), 0);
    lv_obj_clear_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);
  }
}

static void update_toast(net_app_t *app) {
  if (!app || !app->toast_bar) return;
  if (app->toast_expire_ms > 0 && net_get_ms() > app->toast_expire_ms) {
    lv_obj_add_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);
    app->toast_expire_ms = 0;
  }
}

// ---------------------------------------------------------------------------
// Direct Driver IOCTL Helpers
// ---------------------------------------------------------------------------

static bool ioctl_get_radio_power(void) {
  int fd = open_iface_dev("wifi0");
  if (fd < 0) return false;
  int power = 0;
  int ret = ioctl(fd, EOS_RADIO_IOCTL_GET_POWER, &power);
  close(fd);
  return (ret == 0 && power != 0);
}

static bool ioctl_set_radio_power(bool on) {
  int fd = open_iface_dev("wifi0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_RADIO_IOCTL_POWER, on ? 1 : 0);
  close(fd);
  return (ret == 0);
}

static bool ioctl_get_netif_info(const char *dev, eos_netif_info_t *out_info) {
  if (!out_info) return false;
  memset(out_info, 0, sizeof(*out_info));
  int fd = open_iface_dev(dev);
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_NETIF_IOCTL_GET_INFO, out_info);
  close(fd);
  return (ret == 0);
}

static bool ioctl_get_sta_ap_info(eos_sta_ap_info_t *out_ap) {
  if (!out_ap) return false;
  memset(out_ap, 0, sizeof(*out_ap));
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_STA_IOCTL_GET_AP_INFO, out_ap);
  close(fd);
  return (ret == 0);
}

static bool ioctl_sta_connect(const char *ssid, const char *pass) {
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  eos_sta_connect_t conn;
  memset(&conn, 0, sizeof(conn));
  strncpy(conn.ssid, ssid, sizeof(conn.ssid) - 1);
  if (pass && pass[0]) {
    strncpy(conn.password, pass, sizeof(conn.password) - 1);
  }
  conn.timeout_ms = 8000;
  int ret = ioctl(fd, EOS_STA_IOCTL_CONNECT, &conn);
  close(fd);
  return (ret == 0);
}

static bool ioctl_sta_disconnect(void) {
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_STA_IOCTL_DISCONNECT);
  close(fd);
  return (ret == 0);
}

static bool ioctl_scan_aps(net_ap_item_t *out_aps, int max_aps, int *out_count) {
  if (!out_aps || max_aps <= 0 || !out_count) return false;
  *out_count = 0;

  int fd = open_iface_dev("wifi0");
  if (fd < 0) return false;

  eos_wifi_scan_config_t scfg = {
      .mode = EOS_WIFI_SCAN_ACTIVE,
      .channel = 0,
      .show_hidden = false,
      .dwell_time_ms = 120,
  };

  if (ioctl(fd, EOS_RADIO_IOCTL_SCAN_START, &scfg) != 0) {
    close(fd);
    return false;
  }

  uint16_t ap_count = 0;
  ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_COUNT, &ap_count);

  if (ap_count > (uint16_t)max_aps) ap_count = (uint16_t)max_aps;

  if (ap_count > 0) {
    eos_wifi_scan_ap_t *recs = calloc(ap_count, sizeof(eos_wifi_scan_ap_t));
    if (recs) {
      eos_wifi_scan_results_t res = {
          .aps = recs,
          .max_count = ap_count,
          .count = 0,
      };
      if (ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_RESULTS, &res) == 0) {
        *out_count = (int)res.count;
        for (size_t i = 0; i < res.count; i++) {
          const char *name = recs[i].is_hidden ? "<Hidden>" : recs[i].ssid;
          strncpy(out_aps[i].ssid, name[0] ? name : "<Hidden>", sizeof(out_aps[i].ssid) - 1);
          out_aps[i].rssi = recs[i].rssi;
          out_aps[i].channel = recs[i].channel;
          out_aps[i].authmode = recs[i].authmode;
        }
      }
      free(recs);
    }
  }

  close(fd);
  return true;
}

static bool ioctl_get_ap_config(eos_ap_config_t *out_cfg) {
  if (!out_cfg) return false;
  memset(out_cfg, 0, sizeof(*out_cfg));
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_AP_IOCTL_GET_CONFIG, out_cfg);
  close(fd);
  if (ret != 0 || out_cfg->ssid[0] == '\0') {
    strlcpy(out_cfg->ssid, "EOS-Hotspot", sizeof(out_cfg->ssid));
    strlcpy(out_cfg->password, "eos12345678", sizeof(out_cfg->password));
    out_cfg->channel = 1;
    out_cfg->authmode = 3; // WPA2_PSK
    out_cfg->max_connection = 4;
  }
  return true;
}

static bool ioctl_set_ap_config(const eos_ap_config_t *cfg) {
  if (!cfg) return false;
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_AP_IOCTL_SET_CONFIG, cfg);
  close(fd);
  return (ret == 0);
}

static bool ioctl_set_ap_power(bool on, const eos_ap_config_t *cfg) {
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;

  int ret = 0;
  if (on) {
    if (cfg) {
      ioctl(fd, EOS_AP_IOCTL_SET_CONFIG, cfg);
    }
    ret = ioctl(fd, EOS_NETIF_IOCTL_SET_UP);
  } else {
    ret = ioctl(fd, EOS_NETIF_IOCTL_SET_DOWN);
  }
  close(fd);
  return (ret == 0);
}

static bool ioctl_toggle_dhcpc(void) {
  eos_netif_info_t info;
  if (!ioctl_get_netif_info("sta0", &info)) return false;
  bool is_dhcp = (info.flags & EOS_NETIF_FLAG_DHCP_CLIENT) != 0;
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  int ret = ioctl(fd, is_dhcp ? EOS_NETIF_IOCTL_STOP_DHCPC : EOS_NETIF_IOCTL_START_DHCPC);
  close(fd);
  return (ret == 0);
}

static bool ioctl_toggle_dhcps(void) {
  eos_netif_info_t info;
  if (!ioctl_get_netif_info("ap0", &info)) return false;
  bool is_dhcp = (info.flags & EOS_NETIF_FLAG_DHCP_SERVER) != 0;
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;
  int ret = ioctl(fd, is_dhcp ? EOS_NETIF_IOCTL_STOP_DHCPS : EOS_NETIF_IOCTL_START_DHCPS);
  close(fd);
  return (ret == 0);
}

static bool ioctl_set_sta_static_ip(const char *ip, const char *mask, const char *gw) {
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  eos_netif_ip_info_t ip_info;
  memset(&ip_info, 0, sizeof(ip_info));
  strlcpy(ip_info.ip, (ip && ip[0]) ? ip : "192.168.1.150", sizeof(ip_info.ip));
  strlcpy(ip_info.netmask, (mask && mask[0]) ? mask : "255.255.255.0", sizeof(ip_info.netmask));
  strlcpy(ip_info.gw, (gw && gw[0]) ? gw : "192.168.1.1", sizeof(ip_info.gw));
  int ret = ioctl(fd, EOS_NETIF_IOCTL_SET_IP, &ip_info);
  close(fd);
  return (ret == 0);
}

static bool ioctl_get_bridge_info(eos_netif_bridge_info_t *out_binfo) {
  if (!out_binfo) return false;
  memset(out_binfo, 0, sizeof(*out_binfo));
  int fd = open_iface_dev("br0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_GET_INFO, out_binfo);
  close(fd);
  return (ret == 0);
}

static bool ioctl_set_bridge_napt(bool on) {
  int fd = open_iface_dev("br0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_SET_NAPT, on);
  close(fd);
  return (ret == 0);
}

static bool ioctl_set_promisc(bool on) {
  int fd = open_iface_dev("wifi0");
  if (fd < 0) return false;
  eos_wifi_promisc_cfg_t pcfg = {
      .enable = on,
      .filter_mgmt = true,
      .filter_ctrl = true,
      .filter_data = true,
      .filter_misc = true,
  };
  int ret = ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg);
  close(fd);
  return (ret == 0);
}

static bool ioctl_toggle_iface(const char *dev) {
  eos_netif_info_t info;
  if (!ioctl_get_netif_info(dev, &info)) return false;
  bool is_up = (info.flags & EOS_NETIF_FLAG_UP) != 0;
  int fd = open_iface_dev(dev);
  if (fd < 0) return false;
  int ret = ioctl(fd, is_up ? EOS_NETIF_IOCTL_SET_DOWN : EOS_NETIF_IOCTL_SET_UP);
  close(fd);
  return (ret == 0);
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
  lv_obj_set_size(sw, 36, 18);
  lv_obj_set_style_bg_color(sw, lv_color_hex(0x334155), 0);
  lv_obj_set_style_bg_color(sw, lv_color_hex(COL_GREEN), LV_STATE_CHECKED);
  lv_obj_set_style_pad_all(sw, 0, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
}

static void item_click_event_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app || app->dialog_active) return;
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

static lv_obj_t *add_switch_row(net_app_t *app, lv_obj_t *parent, const char *label_text,
                                lv_obj_t **out_sw, bool state, net_item_action_t action) {
  if (app->item_count >= NET_MAX_ITEMS) return NULL;
  int idx = app->item_count++;

  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), 26);
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
  if (app->item_count >= NET_MAX_ITEMS) return NULL;
  int idx = app->item_count++;

  lv_obj_t *btn = lv_obj_create(parent);
  lv_obj_set_size(btn, lv_pct(100), 24);
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
  if (app->item_count >= NET_MAX_ITEMS) return NULL;
  int idx = app->item_count++;

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
  for (int i = 0; i < app->item_count; i++) {
    lv_obj_t *item = app->items[i];
    if (!item) continue;
    if (i == app->sel_idx) {
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
  bool new_state = !app->radio_on;
  if (ioctl_set_radio_power(new_state)) {
    app->radio_on = new_state;
    set_status(app, new_state ? 3 : 1, "Wi-Fi radio turned %s", new_state ? "ON" : "OFF");
  } else {
    set_status(app, 2, "Failed to toggle Wi-Fi radio");
  }
  net_render_active_tab(app);
}

static void do_scan(net_app_t *app) {
  set_status(app, 0, "Scanning Wi-Fi networks...");
  net_render_active_tab(app);
  eos_lvgl_step(20);

  int count = 0;
  if (ioctl_scan_aps(app->aps, NET_MAX_SCAN_APS, &count)) {
    app->ap_count = count;
    app->radio_on = true;
    set_status(app, 3, "Scan complete: %d network(s) found", count);
  } else {
    set_status(app, 2, "Wi-Fi scan failed (ensure radio is ON)");
  }
  net_render_active_tab(app);
}

static void do_connect_ap(net_app_t *app, const char *ssid, const char *pass) {
  if (!ssid || !ssid[0]) return;
  strncpy(app->connect_ssid, ssid, sizeof(app->connect_ssid) - 1);

  set_status(app, 0, "Connecting to \"%.24s\"...", ssid);
  net_render_active_tab(app);
  eos_lvgl_step(20);

  if (ioctl_sta_connect(ssid, pass)) {
    app->sta_connected = true;
    set_status(app, 3, "Connected to \"%.24s\"", ssid);
  } else {
    set_status(app, 2, "Failed to connect to \"%.24s\"", ssid);
  }
  net_render_active_tab(app);
}

static void net_confirm_disconnect_cb(bool confirmed, void *user_data) {
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;
  app->dialog_active = false;
  app->dialog_overlay = NULL;
  if (confirmed) {
    ioctl_sta_disconnect();
    app->sta_connected = false;
    set_status(app, 1, "Disconnected from network");
    net_render_active_tab(app);
  }
}

static void do_disconnect(net_app_t *app) {
  if (!app || app->dialog_active) return;
  app->dialog_active = true;
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
  bool new_state = !app->ap_on;
  if (ioctl_set_ap_power(new_state, &app->ap_cfg)) {
    app->ap_on = new_state;
    set_status(app, new_state ? 3 : 1, "SoftAP %s (\"%s\", Ch%u)",
               new_state ? "started" : "stopped",
               app->ap_cfg.ssid, (unsigned)app->ap_cfg.channel);
  } else {
    set_status(app, 2, "Failed to toggle SoftAP");
  }
  net_render_active_tab(app);
}

static void do_toggle_napt(net_app_t *app) {
  bool new_state = !app->napt_on;
  if (ioctl_set_bridge_napt(new_state)) {
    app->napt_on = new_state;
    set_status(app, new_state ? 3 : 1, "Bridge NAPT routing %s", new_state ? "enabled" : "disabled");
  } else {
    set_status(app, 2, "Failed to configure Bridge NAPT");
  }
  net_render_active_tab(app);
}

static void do_toggle_promisc(net_app_t *app) {
  bool new_state = !app->promisc_on;
  if (ioctl_set_promisc(new_state)) {
    app->promisc_on = new_state;
    set_status(app, new_state ? 3 : 1, "802.11 monitor mode %s", new_state ? "enabled" : "disabled");
  } else {
    set_status(app, 2, "Failed to toggle monitor mode");
  }
  net_render_active_tab(app);
}

static void handle_item_action(net_app_t *app, int idx) {
  if (idx < 0 || idx >= app->item_count) return;

  net_item_action_t act = app->item_actions[idx];
  int arg = app->item_args[idx];

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
      if (arg >= 0 && arg < app->ap_count) {
        if (app->aps[arg].authmode > 0) {
          app->dialog_ap_idx = arg;
          char pbuf[96];
          snprintf(pbuf, sizeof(pbuf), "Enter password for \"%.24s\":", app->aps[arg].ssid);
          net_open_dialog(app, DIALOG_CONNECT_AP_PW, "Connect to Wi-Fi", pbuf, "", true);
        } else {
          do_connect_ap(app, app->aps[arg].ssid, "");
        }
      }
      break;

    case ITEM_ACTION_CONNECT_MANUAL:
      net_open_dialog(app, DIALOG_MANUAL_SSID, "Connect to Wi-Fi",
                      "Enter Network SSID:", "", false);
      break;

    case ITEM_ACTION_TOGGLE_DHCPC:
      if (ioctl_toggle_dhcpc()) {
        set_status(app, 3, "Station DHCP client toggled");
      } else {
        set_status(app, 2, "Failed to toggle DHCP client");
      }
      net_render_active_tab(app);
      break;

    case ITEM_ACTION_EDIT_STATIC_IP:
      net_open_dialog(app, DIALOG_STA_STATIC_IP, "Set Static IP",
                      "IP [Mask GW] (e.g. 192.168.1.150):", "192.168.1.150", false);
      break;

    case ITEM_ACTION_TOGGLE_AP:
      do_toggle_ap(app);
      break;

    case ITEM_ACTION_TOGGLE_DHCPS:
      if (ioctl_toggle_dhcps()) {
        set_status(app, 3, "SoftAP DHCP server toggled");
      } else {
        set_status(app, 2, "Failed to toggle DHCP server");
      }
      net_render_active_tab(app);
      break;

    case ITEM_ACTION_EDIT_AP_SSID:
      net_open_dialog(app, DIALOG_AP_SSID, "SoftAP SSID",
                      "Enter Hotspot SSID:", app->ap_cfg.ssid, false);
      break;

    case ITEM_ACTION_EDIT_AP_PASS:
      net_open_dialog(app, DIALOG_AP_PASS, "SoftAP Password",
                      "Enter Password (>=8 chars, or empty for Open):", app->ap_cfg.password, true);
      break;

    case ITEM_ACTION_CYCLE_AP_CHAN: {
      uint8_t ch = app->ap_cfg.channel;
      // Cycle: 1 -> 6 -> 11 -> 2 -> 3 -> 4 -> 5 -> 7 -> 8 -> 9 -> 10 -> 12 -> 13 -> 1
      if (ch == 1) ch = 6;
      else if (ch == 6) ch = 11;
      else if (ch == 11) ch = 2;
      else if (ch >= 13) ch = 1;
      else ch++;
      app->ap_cfg.channel = ch;
      ioctl_set_ap_config(&app->ap_cfg);
      set_status(app, 3, "SoftAP channel set to Ch %u (%u MHz)",
                 (unsigned)ch, (unsigned)(2407 + ch * 5));
      net_render_active_tab(app);
      break;
    }

    case ITEM_ACTION_TOGGLE_AP_SECURITY: {
      if (app->ap_cfg.authmode == 0) {
        app->ap_cfg.authmode = 3;
        if (strlen(app->ap_cfg.password) < 8) {
          strlcpy(app->ap_cfg.password, "eos12345678", sizeof(app->ap_cfg.password));
        }
        ioctl_set_ap_config(&app->ap_cfg);
        set_status(app, 3, "SoftAP security set to WPA2-PSK (password: %s)", app->ap_cfg.password);
      } else {
        app->ap_cfg.authmode = 0;
        app->ap_cfg.password[0] = '\0';
        ioctl_set_ap_config(&app->ap_cfg);
        set_status(app, 3, "SoftAP security set to Open (no encryption)");
      }
      net_render_active_tab(app);
      break;
    }

    case ITEM_ACTION_TOGGLE_NAPT:
      do_toggle_napt(app);
      break;

    case ITEM_ACTION_TOGGLE_IFACE: {
      const char *devs[] = {"wifi0", "sta0", "ap0", "br0"};
      if (arg >= 0 && arg < 4) {
        ioctl_toggle_iface(devs[arg]);
        net_render_active_tab(app);
      }
      break;
    }

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
    app->dialog_active = false;
    app->dialog_type = DIALOG_NONE;
  }
}

static void net_dialog_cancel_cb(const char *unused, void *user_data) {
  (void)unused;
  net_app_t *app = (net_app_t *)user_data;
  if (app) {
    app->dialog_active = false;
    app->dialog_type = DIALOG_NONE;
    app->dialog_overlay = NULL;
  }
}

static void net_dialog_submit_cb(const char *text, void *user_data) {
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;
  net_dialog_type_t dtype = app->dialog_type;
  int idx = app->dialog_ap_idx;
  app->dialog_active = false;
  app->dialog_type = DIALOG_NONE;
  app->dialog_overlay = NULL;

  switch (dtype) {
    case DIALOG_CONNECT_AP_PW:
      if (idx >= 0 && idx < app->ap_count) {
        do_connect_ap(app, app->aps[idx].ssid, text ? text : "");
      }
      break;

    case DIALOG_MANUAL_SSID:
      if (text && text[0]) {
        strlcpy(app->dialog_buf, text, sizeof(app->dialog_buf));
        net_open_dialog(app, DIALOG_MANUAL_PW, "Wi-Fi Password",
                        "Enter password (empty for Open network):", "", true);
      }
      break;

    case DIALOG_MANUAL_PW:
      if (app->dialog_buf[0]) {
        do_connect_ap(app, app->dialog_buf, text ? text : "");
        app->dialog_buf[0] = '\0';
      }
      break;

    case DIALOG_AP_SSID:
      if (text && text[0]) {
        strlcpy(app->ap_cfg.ssid, text, sizeof(app->ap_cfg.ssid));
        ioctl_set_ap_config(&app->ap_cfg);
        set_status(app, 3, "SoftAP SSID updated to \"%s\"", app->ap_cfg.ssid);
        net_render_active_tab(app);
      }
      break;

    case DIALOG_AP_PASS:
      if (!text || text[0] == '\0') {
        app->ap_cfg.authmode = 0; // OPEN
        app->ap_cfg.password[0] = '\0';
        ioctl_set_ap_config(&app->ap_cfg);
        set_status(app, 3, "SoftAP encryption set to Open (no password)");
      } else if (strlen(text) < 8) {
        set_status(app, 1, "Password must be >= 8 chars or empty for Open");
      } else {
        app->ap_cfg.authmode = 3; // WPA2_PSK
        strlcpy(app->ap_cfg.password, text, sizeof(app->ap_cfg.password));
        ioctl_set_ap_config(&app->ap_cfg);
        set_status(app, 3, "SoftAP password updated (WPA2-PSK)");
      }
      net_render_active_tab(app);
      break;

    case DIALOG_STA_STATIC_IP:
      if (text && text[0]) {
        char ip[16] = {0}, mask[16] = "255.255.255.0", gw[16] = "192.168.1.1";
        sscanf(text, "%15s %15s %15s", ip, mask, gw);
        if (ip[0]) {
          if (ioctl_set_sta_static_ip(ip, mask, gw)) {
            set_status(app, 3, "Static IP configured: %s", ip);
          } else {
            set_status(app, 2, "Failed to set static IP");
          }
          net_render_active_tab(app);
        }
      }
      break;

    default:
      break;
  }
}

static void net_open_dialog(net_app_t *app, net_dialog_type_t type, const char *title,
                           const char *prompt, const char *initial, bool is_pw) {
  if (!app || app->dialog_active) return;
  app->dialog_active = true;
  app->dialog_type = type;

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

static void net_render_active_tab(net_app_t *app) {
  if (!app || !app->content_cont) return;

  int32_t saved_scroll_y = lv_obj_get_scroll_y(app->content_cont);

  lv_obj_clean(app->content_cont);
  memset(app->items, 0, sizeof(app->items));
  memset(app->item_actions, 0, sizeof(app->item_actions));
  memset(app->item_args, 0, sizeof(app->item_args));
  app->item_count = 0;
  app->sw_radio = NULL;
  app->sw_ap = NULL;
  app->sw_dhcpc = NULL;
  app->sw_dhcps = NULL;
  app->sw_napt = NULL;
  app->sw_promisc = NULL;

  // Refresh hardware cached states directly via IOCTL
  app->radio_on = ioctl_get_radio_power();

  eos_netif_info_t sta_info;
  bool has_sta = ioctl_get_netif_info("sta0", &sta_info);
  app->sta_connected = has_sta && ((sta_info.flags & EOS_NETIF_FLAG_LINK_UP) != 0);
  app->sta_dhcp_on = has_sta && ((sta_info.flags & EOS_NETIF_FLAG_DHCP_CLIENT) != 0);

  eos_netif_info_t ap_info;
  bool has_ap = ioctl_get_netif_info("ap0", &ap_info);
  app->ap_on = has_ap && ((ap_info.flags & EOS_NETIF_FLAG_UP) != 0);
  app->ap_dhcp_on = has_ap && ((ap_info.flags & EOS_NETIF_FLAG_DHCP_SERVER) != 0);

  eos_netif_bridge_info_t br_info;
  bool has_br = ioctl_get_bridge_info(&br_info);
  app->napt_on = has_br && br_info.napt_enabled;

  // Update tab bar label
  char tbar_str[64];
  snprintf(tbar_str, sizeof(tbar_str), "<  %s (%d/%d)  >",
           tab_titles[app->cur_tab], app->cur_tab + 1, NET_NUM_TABS);
  lv_label_set_text(app->tab_label, tbar_str);

  // Update toolbar hint file
  eos_toolbar_set(tab_titles[app->cur_tab], "[▲▼]Select  [◄►]Tab  [A]Action  [B]Exit");

  // =========================================================================
  // PAGE 1: Wi-Fi Station
  // =========================================================================
  if (app->cur_tab == TAB_STA) {
    // 1. Radio Power switch row
    add_switch_row(app, app->content_cont, "Wi-Fi Radio Power", &app->sw_radio,
                   app->radio_on, ITEM_ACTION_TOGGLE_RADIO);

    // 2. DHCP Client switch row
    add_switch_row(app, app->content_cont, "DHCP Client (sta0)", &app->sw_dhcpc,
                   app->sta_dhcp_on, ITEM_ACTION_TOGGLE_DHCPC);

    // 3. Connection status card
    eos_sta_ap_info_t sta_ap;
    ioctl_get_sta_ap_info(&sta_ap);

    char title[64];
    char desc[256];
    uint32_t tcol = COL_TEXT_SECOND;

    if (app->sta_connected && sta_ap.ssid[0]) {
      snprintf(title, sizeof(title), "Connected: %s", sta_ap.ssid);
      tcol = COL_GREEN;
      snprintf(desc, sizeof(desc),
               "Signal: %d dBm (Ch %u)\nIP: %s  Mask: %s\nGW: %s\nMode: %s",
               (int)sta_ap.rssi, (unsigned)sta_ap.channel,
               sta_info.ip.ip[0] ? sta_info.ip.ip : "0.0.0.0",
               sta_info.ip.netmask[0] ? sta_info.ip.netmask : "0.0.0.0",
               sta_info.ip.gw[0] ? sta_info.ip.gw : "0.0.0.0",
               app->sta_dhcp_on ? "DHCP Auto-Assigned" : "Static IP");
    } else if (has_sta && (sta_info.flags & EOS_NETIF_FLAG_UP)) {
      snprintf(title, sizeof(title), "Station Interface UP (Idle)");
      tcol = COL_AMBER;
      snprintf(desc, sizeof(desc),
               "Ready to connect. MAC: %02x:%02x:%02x:%02x:%02x:%02x\nDHCP Client: %s",
               sta_info.mac[0], sta_info.mac[1], sta_info.mac[2],
               sta_info.mac[3], sta_info.mac[4], sta_info.mac[5],
               app->sta_dhcp_on ? "Enabled" : "Disabled (Static)");
    } else {
      snprintf(title, sizeof(title), "Station Interface DOWN");
      tcol = COL_TEXT_SECOND;
      snprintf(desc, sizeof(desc), "Turn radio ON or scan to connect.");
    }

    add_card(app, app->content_cont, title, desc, tcol, ITEM_ACTION_NONE, 0);

    // 4. Action buttons
    add_button_row(app, app->content_cont, "[ Scan Wi-Fi Networks ]", "", COL_ACCENT_BLUE,
                   ITEM_ACTION_SCAN, 0);
    add_button_row(app, app->content_cont, "[ Connect to Hidden / Manual SSID ]", "", COL_BG_CARD,
                   ITEM_ACTION_CONNECT_MANUAL, 0);

    if (!app->sta_dhcp_on) {
      char ip_badge[32];
      snprintf(ip_badge, sizeof(ip_badge), "%s [Edit]",
               sta_info.ip.ip[0] ? sta_info.ip.ip : "192.168.1.150");
      add_button_row(app, app->content_cont, "Static IP Config:", ip_badge, COL_BG_CARD,
                     ITEM_ACTION_EDIT_STATIC_IP, 0);
    }

    if (app->sta_connected) {
      add_button_row(app, app->content_cont, "[ Disconnect ]", "", COL_RED_DARK,
                     ITEM_ACTION_DISCONNECT, 0);
    }

    // 5. Discovered networks
    if (app->ap_count > 0) {
      lv_obj_t *lhdr = lv_label_create(app->content_cont);
      lv_label_set_text(lhdr, "Available Networks:");
      lv_obj_set_style_text_color(lhdr, lv_color_hex(COL_ACCENT_SKY), 0);

      for (int i = 0; i < app->ap_count; i++) {
        char badge[32];
        const char *sec = eos_net_wifi_auth_name(app->aps[i].authmode);
        snprintf(badge, sizeof(badge), "%d dBm Ch%u %s",
                 (int)app->aps[i].rssi, (unsigned)app->aps[i].channel, sec);
        add_button_row(app, app->content_cont, app->aps[i].ssid, badge, COL_BG_CARD,
                       ITEM_ACTION_CONNECT_AP, i);
      }
    }
  }

  // =========================================================================
  // PAGE 2: Access Point (SoftAP)
  // =========================================================================
  else if (app->cur_tab == TAB_AP) {
    // 1. AP-STA Shared Radio Channel Notice
    eos_sta_ap_info_t sta_ap;
    memset(&sta_ap, 0, sizeof(sta_ap));
    ioctl_get_sta_ap_info(&sta_ap);

    if (app->sta_connected && sta_ap.channel > 0) {
      char notice_desc[256];
      snprintf(notice_desc, sizeof(notice_desc),
               "Single PHY radio shared in concurrent AP+STA mode.\n"
               "SoftAP channel is locked to Station channel: Ch %u (%u MHz).\n"
               "(Configured SoftAP channel: Ch %u)",
               (unsigned)sta_ap.channel, (unsigned)(2407 + sta_ap.channel * 5),
               (unsigned)app->ap_cfg.channel);
      add_card(app, app->content_cont, "! AP-STA Shared Channel Active", notice_desc,
               COL_AMBER, ITEM_ACTION_NONE, 0);
    } else {
      add_card(app, app->content_cont, "i AP-STA Shared Channel Rule",
               "ESP32 shares 1 RF transceiver. When Station connects\n"
               "to a router, SoftAP automatically shifts to the\n"
               "router's channel to maintain concurrent link.",
               COL_ACCENT_SKY, ITEM_ACTION_NONE, 0);
    }

    // 2. SoftAP Power Switch
    add_switch_row(app, app->content_cont, "SoftAP Power", &app->sw_ap,
                   app->ap_on, ITEM_ACTION_TOGGLE_AP);

    // 3. DHCP Server Switch
    add_switch_row(app, app->content_cont, "DHCP Server (ap0)", &app->sw_dhcps,
                   app->ap_dhcp_on, ITEM_ACTION_TOGGLE_DHCPS);

    // 4. SoftAP Configuration Controls
    char ssid_badge[48];
    snprintf(ssid_badge, sizeof(ssid_badge), "\"%s\" [Edit]", app->ap_cfg.ssid);
    add_button_row(app, app->content_cont, "SoftAP SSID:", ssid_badge, COL_BG_CARD,
                   ITEM_ACTION_EDIT_AP_SSID, 0);

    char sec_title[32];
    char sec_badge[48];
    const char *auth_str = (app->ap_cfg.authmode == 0) ? "Open" : "WPA2-PSK";
    snprintf(sec_title, sizeof(sec_title), "Security: %s", auth_str);
    snprintf(sec_badge, sizeof(sec_badge), "%s [Toggle]",
             (app->ap_cfg.authmode == 0) ? "No Pass" : "Encrypted");
    add_button_row(app, app->content_cont, sec_title, sec_badge, COL_BG_CARD,
                   ITEM_ACTION_TOGGLE_AP_SECURITY, 0);

    if (app->ap_cfg.authmode != 0) {
      char pw_masked[32];
      size_t pw_len = strlen(app->ap_cfg.password);
      if (pw_len > 0) {
        memset(pw_masked, '*', sizeof(pw_masked));
        if (pw_len > 10) pw_len = 10;
        pw_masked[pw_len] = '\0';
      } else {
        strlcpy(pw_masked, "(none)", sizeof(pw_masked));
      }
      char pw_badge[48];
      snprintf(pw_badge, sizeof(pw_badge), "%s [Change]", pw_masked);
      add_button_row(app, app->content_cont, "Password:", pw_badge, COL_BG_CARD,
                     ITEM_ACTION_EDIT_AP_PASS, 0);
    }

    char chan_title[48];
    char chan_badge[48];
    uint8_t eff_chan = (app->sta_connected && sta_ap.channel > 0) ? sta_ap.channel : app->ap_cfg.channel;
    snprintf(chan_title, sizeof(chan_title), "Channel: Ch %u (%u MHz)",
             (unsigned)eff_chan, (unsigned)(2407 + eff_chan * 5));
    snprintf(chan_badge, sizeof(chan_badge), "[Cycle 1-13]");
    add_button_row(app, app->content_cont, chan_title, chan_badge, COL_BG_CARD,
                   ITEM_ACTION_CYCLE_AP_CHAN, 0);

    // 5. Active Status & Connected Clients Card
    char ap_desc[256];
    if (app->ap_on) {
      snprintf(ap_desc, sizeof(ap_desc),
               "SSID: %s  Ch: %u\nIP: %s  Mask: %s\nMAC: %02x:%02x:%02x:%02x:%02x:%02x\nDHCP Server: %s",
               app->ap_cfg.ssid, (unsigned)eff_chan,
               ap_info.ip.ip[0] ? ap_info.ip.ip : "192.168.4.1",
               ap_info.ip.netmask[0] ? ap_info.ip.netmask : "255.255.255.0",
               ap_info.mac[0], ap_info.mac[1], ap_info.mac[2],
               ap_info.mac[3], ap_info.mac[4], ap_info.mac[5],
               app->ap_dhcp_on ? "Enabled" : "Disabled");
      add_card(app, app->content_cont, "SoftAP Active (UP)", ap_desc, COL_GREEN, ITEM_ACTION_NONE, 0);

      // Connected clients
      int ap_fd = open_iface_dev("ap0");
      eos_ap_sta_list_t stas;
      memset(&stas, 0, sizeof(stas));
      if (ap_fd >= 0) {
        ioctl(ap_fd, EOS_AP_IOCTL_GET_STAS, &stas);
        close(ap_fd);
      }
      char c_desc[128];
      snprintf(c_desc, sizeof(c_desc), "Connected stations: %u / 4", (unsigned)stas.count);
      add_card(app, app->content_cont, "Hotspot Clients", c_desc, COL_ACCENT_SKY, ITEM_ACTION_NONE, 0);

      add_button_row(app, app->content_cont, "[ Stop Hotspot ]", "", COL_RED_DARK,
                     ITEM_ACTION_TOGGLE_AP, 0);
    } else {
      snprintf(ap_desc, sizeof(ap_desc),
               "Hotspot is disabled.\nConfigured SSID: \"%s\"\nChannel: %u  Security: %s\nDefault IP: 192.168.4.1",
               app->ap_cfg.ssid, (unsigned)app->ap_cfg.channel, auth_str);
      add_card(app, app->content_cont, "SoftAP Inactive", ap_desc, COL_TEXT_SECOND, ITEM_ACTION_NONE, 0);

      add_button_row(app, app->content_cont, "[ Start Hotspot ]", "", COL_ACCENT_BLUE,
                     ITEM_ACTION_TOGGLE_AP, 0);
    }
  }

  // =========================================================================
  // PAGE 3: Bridge & NAPT
  // =========================================================================
  else if (app->cur_tab == TAB_BRIDGE) {
    add_switch_row(app, app->content_cont, "Bridge NAT (NAPT)", &app->sw_napt,
                   app->napt_on, ITEM_ACTION_TOGGLE_NAPT);

    char bdesc[256];
    if (has_br) {
      char ports_str[128] = {0};
      for (int i = 0; i < br_info.port_count; i++) {
        if (i > 0) strlcat(ports_str, ", ", sizeof(ports_str));
        strlcat(ports_str, br_info.ports[i], sizeof(ports_str));
      }
      snprintf(bdesc, sizeof(bdesc),
               "Bridge: %s\nPorts (%u): %s\nNAPT: %s",
               br_info.name[0] ? br_info.name : "br0",
               (unsigned)br_info.port_count,
               ports_str[0] ? ports_str : "(none)",
               br_info.napt_enabled ? "Active" : "Disabled");
      add_card(app, app->content_cont, "Virtual Bridge Active", bdesc, COL_GREEN, ITEM_ACTION_NONE, 0);
    } else {
      snprintf(bdesc, sizeof(bdesc),
               "Bridge br0 is not attached.\nAttach via: devctl attach net bridge");
      add_card(app, app->content_cont, "Bridge Inactive", bdesc, COL_TEXT_SECOND, ITEM_ACTION_NONE, 0);
    }
  }

  // =========================================================================
  // PAGE 4: All Network Interfaces
  // =========================================================================
  else if (app->cur_tab == TAB_IFACES) {
    // Hardware controller card
    eos_netif_info_t w_info;
    bool has_w = ioctl_get_netif_info("wifi0", &w_info);

    char hdesc[128];
    snprintf(hdesc, sizeof(hdesc),
             "Controller: %s\nMAC: %02x:%02x:%02x:%02x:%02x:%02x",
             has_w ? (w_info.flags & EOS_NETIF_FLAG_UP ? "Radio UP" : "Radio DOWN") : "Unknown",
             w_info.mac[0], w_info.mac[1], w_info.mac[2],
             w_info.mac[3], w_info.mac[4], w_info.mac[5]);
    add_card(app, app->content_cont, "WLAN PHY Controller (wifi0)", hdesc, COL_ACCENT_SKY,
             ITEM_ACTION_TOGGLE_RADIO, 0);

    const char *devs[] = {"wifi0", "sta0", "ap0", "br0"};
    for (int d = 0; d < 4; d++) {
      eos_netif_info_t inf;
      if (!ioctl_get_netif_info(devs[d], &inf)) continue;

      bool is_up = (inf.flags & EOS_NETIF_FLAG_UP) != 0;
      bool is_link = (inf.flags & EOS_NETIF_FLAG_LINK_UP) != 0;
      bool has_dhcpc = (inf.flags & EOS_NETIF_FLAG_DHCP_CLIENT) != 0;
      bool has_dhcps = (inf.flags & EOS_NETIF_FLAG_DHCP_SERVER) != 0;

      char card_title[64];
      snprintf(card_title, sizeof(card_title), "%s  [%s%s]", devs[d],
               is_up ? "UP" : "DOWN",
               is_link ? ", LINK" : "");

      char card_desc[256];
      snprintf(card_desc, sizeof(card_desc),
               "IP: %s  Mask: %s\nMAC: %02x:%02x:%02x:%02x:%02x:%02x\nDHCP: %s\n[A: Toggle UP/DOWN]",
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

  // =========================================================================
  // PAGE 5: 802.11 Sniffer
  // =========================================================================
  else if (app->cur_tab == TAB_SNIFFER) {
    add_switch_row(app, app->content_cont, "802.11 Monitor Mode", &app->sw_promisc,
                   app->promisc_on, ITEM_ACTION_TOGGLE_PROMISC);

    char sdesc[256];
    snprintf(sdesc, sizeof(sdesc),
             "Promiscuous mode captures raw 802.11 MAC\n"
             "frames from /dev/wifi0 via read().\n"
             "Status: %s",
             app->promisc_on ? "MONITORING ACTIVE" : "Inactive");
    add_card(app, app->content_cont, "Raw Packet Capture", sdesc,
             app->promisc_on ? COL_GREEN : COL_TEXT_SECOND,
             ITEM_ACTION_TOGGLE_PROMISC, 0);

    if (app->promisc_on) {
      add_card(app, app->content_cont, "! Conflict Warning",
               "Monitor mode interrupts normal Wi-Fi association.",
               COL_AMBER, ITEM_ACTION_NONE, 0);
    }
  }

  if (app->sel_idx >= app->item_count) {
    app->sel_idx = app->item_count > 0 ? app->item_count - 1 : 0;
  }
  if (saved_scroll_y > 0) {
    lv_obj_scroll_to_y(app->content_cont, saved_scroll_y, LV_ANIM_OFF);
  }
  net_update_selection_highlight(app);
}

static void net_gesture_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app || app->dialog_active) return;
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_LEFT) {
    app->cur_tab = (app->cur_tab + 1) % NET_NUM_TABS;
    app->sel_idx = 0;
    net_render_active_tab(app);
  } else if (dir == LV_DIR_RIGHT) {
    app->cur_tab = (app->cur_tab - 1 + NET_NUM_TABS) % NET_NUM_TABS;
    app->sel_idx = 0;
    net_render_active_tab(app);
  }
}

// ---------------------------------------------------------------------------
// Hardware Key Handler
// ---------------------------------------------------------------------------

static void net_handle_key(net_app_t *app, uint16_t key) {
  if (app->dialog_active) {
    if (key == KEY_ESC) {
      net_dialog_close(app);
    }
    return;
  }

  switch (key) {
    case KEY_UP:
      if (app->item_count > 0) {
        app->sel_idx = (app->sel_idx - 1 + app->item_count) % app->item_count;
        net_update_selection_highlight(app);
      } else if (app->content_cont) {
        lv_obj_scroll_by(app->content_cont, 0, 30, LV_ANIM_OFF);
      }
      break;

    case KEY_DOWN:
      if (app->item_count > 0) {
        app->sel_idx = (app->sel_idx + 1) % app->item_count;
        net_update_selection_highlight(app);
      } else if (app->content_cont) {
        lv_obj_scroll_by(app->content_cont, 0, -30, LV_ANIM_OFF);
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
    case KEY_BACKSPACE:
      app->quit = true;
      break;

    default:
      break;
  }
}

static void net_back_btn_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (app) {
    app->quit = true;
  }
}

static void tab_bar_click_event_cb(lv_event_t *e) {
  net_app_t *app = (net_app_t *)lv_event_get_user_data(e);
  if (!app || app->dialog_active) return;
  app->cur_tab = (app->cur_tab + 1) % NET_NUM_TABS;
  app->sel_idx = 0;
  net_render_active_tab(app);
}

// ---------------------------------------------------------------------------
// Main Application Entry
// ---------------------------------------------------------------------------

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
  app->cur_tab = TAB_STA;
  app->sel_idx = 0;

  // Read initial SoftAP configuration from driver
  ioctl_get_ap_config(&app->ap_cfg);

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  // Root screen
  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG_DEEP), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Top Header Bar (compact height 20, with neatly integrated back button)
  app->hdr = lv_obj_create(scr);
  lv_obj_set_size(app->hdr, (int32_t)w, 20);
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

  // Clean, non-shifted exit button placed inside the header on the right
  lv_obj_t *back_btn = eos_lvgl_create_back_btn(app->hdr, net_back_btn_cb, app);
  if (back_btn) {
    lv_obj_set_size(back_btn, 24, 16);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, -2, 0);
  }

  // 2. Tab Selector Bar (height 20)
  int32_t tab_y = 20;
  int32_t tab_h = 20;
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
  lv_obj_set_size(app->toast_bar, (int32_t)w - 4, 18);
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
  if (content_h < 60) content_h = 60;

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

  // Initial render
  net_render_active_tab(app);

  // Main Loop
  while (!app->quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      net_handle_key(app, key);
      if (app->quit) break;
    }

    // Periodic state refresh
    uint64_t now = net_get_ms();
    if (now - app->last_refresh_ms >= 3000ULL) {
      app->last_refresh_ms = now;
      bool r = ioctl_get_radio_power();
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

    update_toast(app);
    eos_lvgl_step(10);
  }

  // Turn off monitor mode if active
  if (app->promisc_on) {
    ioctl_set_promisc(false);
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
