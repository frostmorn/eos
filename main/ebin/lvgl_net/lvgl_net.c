#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/lvgl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define NET_MAX_SCAN_APS 16
#define NET_NUM_TABS     5
#define NET_CMD_BUF_SIZE 256
#define NET_TMP_FILE     "/netctl.out"

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
#define COL_TEXT_DIM      0x64748B
#define COL_ACCENT_BLUE   EOS_COLOR_ACCENT_BLUE
#define COL_ACCENT_SKY    EOS_COLOR_ACCENT_CYAN
#define COL_ACCENT_LTBLUE EOS_COLOR_BORDER_FOCUS
#define COL_GREEN         0x10B981
#define COL_GREEN_DIM     EOS_COLOR_STATUS_GREEN
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

  // Hardware states (cached from CLI queries)
  bool radio_on;
  bool ap_on;
  bool napt_on;
  bool promisc_on;
  bool sta_connected;

  // Scan & Connection
  net_ap_item_t aps[NET_MAX_SCAN_APS];
  int ap_count;
  char connect_ssid[33];
  char connect_pass[64];
  char status_msg[128];
  uint8_t status_type; // 0=info, 1=warning, 2=error, 3=success

  // Password dialog state
  bool pw_dialog_active;
  int pw_dialog_ap_idx;
  char pw_buf[64];
  int pw_cursor;
  lv_obj_t *pw_overlay;
  lv_obj_t *pw_textarea;
  lv_obj_t *pw_kb;

  // Notification toast state
  char toast_msg[128];
  uint64_t toast_expire_ms;

  // UI Widgets
  lv_obj_t *hdr;
  lv_obj_t *hdr_title;
  lv_obj_t *tab_bar;
  lv_obj_t *tab_label;
  lv_obj_t *content_cont;
  lv_obj_t *footer;
  lv_obj_t *footer_lbl;
  lv_obj_t *toast_bar;
  lv_obj_t *toast_lbl;

  // Switches (display-only status indicators within switch rows)
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
// CLI Command Execution Helpers
// ---------------------------------------------------------------------------

static int net_run_cmd(const char *cmd) {
  char full_cmd[NET_CMD_BUF_SIZE + 64];
  snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, NET_TMP_FILE);
  return system(full_cmd);
}

static int net_run_cmd_silent(const char *cmd) {
  return system(cmd);
}

static bool net_read_output(char *buf, size_t bufsz) {
  int fd = open(NET_TMP_FILE, O_RDONLY);
  if (fd < 0) {
    buf[0] = '\0';
    return false;
  }
  ssize_t n = read(fd, buf, bufsz - 1);
  close(fd);
  if (n <= 0) {
    buf[0] = '\0';
    return false;
  }
  buf[n] = '\0';
  return true;
}

static bool net_output_contains(const char *needle) {
  char buf[512];
  if (!net_read_output(buf, sizeof(buf))) return false;
  return strstr(buf, needle) != NULL;
}

// ---------------------------------------------------------------------------
// Hardware State Queries via CLI
// ---------------------------------------------------------------------------

static bool get_radio_power(void) {
  net_run_cmd("netctl wifi0 status");
  return net_output_contains("RADIO:ON");
}

static bool get_sta_connected(void) {
  net_run_cmd("netctl sta0 status");
  return net_output_contains("link=1") || net_output_contains("CONNECTED");
}

static bool get_napt_on(void) {
  net_run_cmd("netctl br0 status");
  return net_output_contains("NAPT: enabled");
}

// ---------------------------------------------------------------------------
// UI Theme & Styling Helpers
// ---------------------------------------------------------------------------

static void style_switch(lv_obj_t *sw) {
  lv_obj_set_size(sw, 38, 20);
  lv_obj_set_style_bg_color(sw, lv_color_hex(COL_BORDER), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sw, lv_color_hex(COL_TEXT_PRIMARY), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
}

static lv_obj_t *create_card(lv_obj_t *parent) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(c, lv_color_hex(COL_BG_CARD), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(COL_BORDER), 0);
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
// Toast Notification System
// ---------------------------------------------------------------------------

static void show_toast(net_app_t *app, const char *msg, uint32_t duration_ms) {
  strncpy(app->toast_msg, msg, sizeof(app->toast_msg) - 1);
  app->toast_expire_ms = net_get_ms() + duration_ms;
  if (app->toast_bar) {
    lv_obj_clear_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(app->toast_lbl, msg);
  }
}

static void update_toast(net_app_t *app) {
  if (app->toast_expire_ms == 0) return;
  if (net_get_ms() >= app->toast_expire_ms) {
    app->toast_expire_ms = 0;
    app->toast_msg[0] = '\0';
    if (app->toast_bar) {
      lv_obj_add_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// ---------------------------------------------------------------------------
// Status Message Helpers
// ---------------------------------------------------------------------------

static void set_status(net_app_t *app, uint8_t type, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(app->status_msg, sizeof(app->status_msg), fmt, args);
  va_end(args);
  app->status_type = type;
}

// ---------------------------------------------------------------------------
// Hardware Actions via CLI
// ---------------------------------------------------------------------------

static void do_scan(net_app_t *app) {
  set_status(app, 0, "Scanning Wi-Fi networks...");
  net_render_active_tab(app);
  eos_lvgl_step(10);

  int rc = net_run_cmd("wlanctl scan");
  if (rc != 0) {
    set_status(app, 2, "Scan failed (radio may be off)");
    net_render_active_tab(app);
    return;
  }

  char buf[2048];
  if (!net_read_output(buf, sizeof(buf))) {
    set_status(app, 2, "Scan returned no output");
    net_render_active_tab(app);
    return;
  }

  // Parse scan output: look for table rows after the header separator "---"
  app->ap_count = 0;
  char *line = buf;
  bool past_header = false;
  while (line && *line && app->ap_count < NET_MAX_SCAN_APS) {
    char *next = strchr(line, '\n');
    if (next) *next = '\0';

    if (strncmp(line, "---", 3) == 0) {
      past_header = true;
    } else if (past_header && strlen(line) > 10) {
      // Parse SSID (first 32 chars), then BSSID, RSSI, CH, AUTH
      net_ap_item_t *ap = &app->aps[app->ap_count];
      memset(ap, 0, sizeof(*ap));

      // Extract SSID from beginning (padded to ~32 chars)
      char ssid_buf[33] = {0};
      strncpy(ssid_buf, line, 32);
      // Trim trailing spaces
      for (int i = 31; i >= 0; i--) {
        if (ssid_buf[i] == ' ' || ssid_buf[i] == '\0')
          ssid_buf[i] = '\0';
        else
          break;
      }
      if (ssid_buf[0] == '\0') strncpy(ssid_buf, "<Hidden>", sizeof(ssid_buf) - 1);
      strncpy(ap->ssid, ssid_buf, sizeof(ap->ssid) - 1);

      // Find RSSI (negative number after BSSID)
      const char *p = line + 32;
      while (*p == ' ') p++;
      // Skip BSSID (17 chars xx:xx:xx:xx:xx:xx)
      if (strlen(p) > 17) p += 17;
      while (*p == ' ') p++;

      // RSSI
      ap->rssi = (int8_t)atoi(p);
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;

      // Channel
      ap->channel = (uint8_t)atoi(p);
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;

      // Auth
      if (strstr(p, "WPA3"))       ap->authmode = 5;
      else if (strstr(p, "WPA2"))  ap->authmode = 3;
      else if (strstr(p, "WPA"))   ap->authmode = 2;
      else if (strstr(p, "WEP"))   ap->authmode = 1;
      else                         ap->authmode = 0;

      app->ap_count++;
    }

    line = next ? next + 1 : NULL;
  }

  set_status(app, 3, "Found %d network(s)", app->ap_count);
  app->radio_on = true;
  net_render_active_tab(app);
}

static void do_connect_ap(net_app_t *app, const char *ssid, const char *pass) {
  if (!ssid || ssid[0] == '\0') return;
  strncpy(app->connect_ssid, ssid, sizeof(app->connect_ssid) - 1);
  if (pass) {
    strncpy(app->connect_pass, pass, sizeof(app->connect_pass) - 1);
  }

  set_status(app, 0, "Connecting to \"%.24s\"...", ssid);
  net_render_active_tab(app);
  eos_lvgl_step(10);

  char cmd[NET_CMD_BUF_SIZE];
  if (pass && pass[0]) {
    snprintf(cmd, sizeof(cmd), "netctl sta0 connect \"%s\" \"%s\"", ssid, pass);
  } else {
    snprintf(cmd, sizeof(cmd), "netctl sta0 connect \"%s\"", ssid);
  }

  int rc = net_run_cmd_silent(cmd);
  if (rc == 0) {
    set_status(app, 3, "Connected to \"%.24s\"", ssid);
    app->sta_connected = true;
  } else {
    set_status(app, 2, "Failed to connect to \"%.24s\"", ssid);
  }

  app->radio_on = get_radio_power();
  net_render_active_tab(app);
}

static void do_disconnect(net_app_t *app) {
  net_run_cmd_silent("netctl sta0 disconnect");
  app->sta_connected = false;
  set_status(app, 0, "Disconnected from AP");
  net_render_active_tab(app);
}

static void do_toggle_radio(net_app_t *app) {
  app->radio_on = !app->radio_on;
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "wlanctl radio %s", app->radio_on ? "on" : "off");
  net_run_cmd_silent(cmd);
  if (!app->radio_on && app->sta_connected) {
    app->sta_connected = false;
    show_toast(app, "Radio off: station disconnected", 3000);
  }
  net_render_active_tab(app);
}

static void do_toggle_ap(net_app_t *app) {
  app->ap_on = !app->ap_on;
  if (app->ap_on) {
    // Check for conflicts
    if (app->promisc_on) {
      show_toast(app, "Warning: sniffer active, AP may be unstable", 4000);
    }
    net_run_cmd_silent("netctl ap0 ap EOS-Hotspot eos12345678 1");
  } else {
    net_run_cmd_silent("netctl ap0 down");
  }
  app->radio_on = get_radio_power();
  net_render_active_tab(app);
}

static void do_toggle_napt(net_app_t *app) {
  app->napt_on = !app->napt_on;
  char cmd[64];
  if (app->napt_on) {
    if (!app->ap_on) {
      show_toast(app, "Enable hotspot first for NAPT routing", 3000);
      app->napt_on = false;
      return;
    }
    if (!app->sta_connected) {
      show_toast(app, "Station must be connected for NAPT", 3000);
      app->napt_on = false;
      return;
    }
    // Ensure bridge is dynamically attached
    net_run_cmd_silent("devctl attach net bridge");
    snprintf(cmd, sizeof(cmd), "netctl br0 bridge sta0");
  } else {
    snprintf(cmd, sizeof(cmd), "netctl br0 bridge off");
  }
  net_run_cmd_silent(cmd);
  net_render_active_tab(app);
}

static void do_toggle_promisc(net_app_t *app) {
  app->promisc_on = !app->promisc_on;
  if (app->promisc_on) {
    // Conflict check
    if (app->sta_connected) {
      show_toast(app, "Warning: sniffer may disrupt Wi-Fi connection", 4000);
    }
    if (app->ap_on) {
      show_toast(app, "Warning: sniffer may affect hotspot clients", 4000);
    }
  }
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "wlanctl sniffer %s", app->promisc_on ? "on" : "off");
  net_run_cmd_silent(cmd);
  app->radio_on = get_radio_power();
  net_render_active_tab(app);
}

// ---------------------------------------------------------------------------
// Password Dialog (using ecore reusable modal input dialog)
// ---------------------------------------------------------------------------

static void pw_dialog_close(net_app_t *app) {
  if (app && app->pw_overlay) {
    eos_lvgl_close_input_dialog(app->pw_overlay);
    app->pw_overlay = NULL;
  }
  if (app) app->pw_dialog_active = false;
}

static void pw_dialog_submit_cb(const char *pass, void *user_data) {
  net_app_t *app = (net_app_t *)user_data;
  if (!app) return;
  int idx = app->pw_dialog_ap_idx;
  app->pw_dialog_active = false;
  app->pw_overlay = NULL;
  if (idx >= 0 && idx < app->ap_count) {
    do_connect_ap(app, app->aps[idx].ssid, pass ? pass : "");
  }
}

static void pw_dialog_cancel_cb(const char *unused, void *user_data) {
  net_app_t *app = (net_app_t *)user_data;
  if (app) {
    app->pw_dialog_active = false;
    app->pw_overlay = NULL;
  }
}

static void pw_dialog_open(net_app_t *app, int ap_idx) {
  if (app->pw_dialog_active) return;
  app->pw_dialog_active = true;
  app->pw_dialog_ap_idx = ap_idx;

  char prompt[96];
  snprintf(prompt, sizeof(prompt), "Enter password for \"%.24s\":", app->aps[ap_idx].ssid);

  eos_lvgl_input_dialog_cfg_t cfg = {
      .title = "Connect to Wi-Fi",
      .prompt = prompt,
      .initial_text = "",
      .ok_label = "Connect",
      .is_password = true,
      .ok_cb = pw_dialog_submit_cb,
      .cancel_cb = pw_dialog_cancel_cb,
      .user_data = app,
  };
  app->pw_overlay = eos_lvgl_create_input_dialog(&cfg);
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
          // If network requires auth, show password dialog
          if (app->aps[ap_idx].authmode >= 1) {
            pw_dialog_open(app, ap_idx);
          } else {
            // Open network, connect directly
            do_connect_ap(app, app->aps[ap_idx].ssid, "");
          }
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
  if (!app || app->pw_dialog_active) return;
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
  if (!app || app->pw_dialog_active) return;
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
  lv_obj_set_style_bg_color(row, lv_color_hex(COL_BG_CARD), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_radius(row, 3, 0);
  lv_obj_set_style_pad_hor(row, 6, 0);
  lv_obj_set_style_pad_ver(row, 2, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
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
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);

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
  lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, 3, 0);
  lv_obj_set_style_pad_hor(btn, 6, 0);
  lv_obj_set_style_pad_ver(btn, 2, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn, item_click_event_cb, LV_EVENT_CLICKED, app);

  lv_obj_t *lbl_txt = lv_label_create(btn);
  lv_label_set_text(lbl_txt, text);
  lv_obj_set_style_text_color(lbl_txt, lv_color_hex(COL_TEXT_PRIMARY), 0);
  lv_obj_align(lbl_txt, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_clear_flag(lbl_txt, LV_OBJ_FLAG_CLICKABLE);

  if (badge && badge[0]) {
    lv_obj_t *lbl_b = lv_label_create(btn);
    lv_label_set_text(lbl_b, badge);
    lv_obj_set_style_text_color(lbl_b, lv_color_hex(COL_ACCENT_LTBLUE), 0);
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

  // Tab titles
  const char *tab_titles[NET_NUM_TABS] = {
      "1. Wi-Fi (Client)",
      "2. Hotspot (AP)",
      "3. Bridge & Routing",
      "4. Interfaces",
      "5. 802.11 Sniffer",
  };
  char tbar_str[64];
  snprintf(tbar_str, sizeof(tbar_str), "<  %s  >  [%d/%d]",
           tab_titles[app->cur_tab], (int)app->cur_tab + 1, NET_NUM_TABS);
  lv_label_set_text(app->tab_label, tbar_str);

  // Status message bar (if present)
  if (app->status_msg[0]) {
    lv_obj_t *sbar = lv_obj_create(app->content_cont);
    lv_obj_set_size(sbar, lv_pct(100), 20);
    lv_obj_set_style_bg_color(sbar, lv_color_hex(COL_BG_CARD), 0);
    lv_obj_set_style_bg_opa(sbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sbar, 0, 0);
    lv_obj_set_style_pad_all(sbar, 2, 0);
    lv_obj_clear_flag(sbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *slbl = lv_label_create(sbar);
    lv_label_set_text(slbl, app->status_msg);
    uint32_t col = (app->status_type == 1) ? COL_AMBER :
                   (app->status_type == 2) ? COL_RED :
                   (app->status_type == 3) ? COL_GREEN :
                   COL_YELLOW;
    lv_obj_set_style_text_color(slbl, lv_color_hex(col), 0);
    lv_obj_align(slbl, LV_ALIGN_LEFT_MID, 2, 0);
  }

  // =========================================================================
  // PAGE 1: Wi-Fi Station
  // =========================================================================
  if (app->cur_tab == TAB_STA) {
    // Row 0: Master Wi-Fi Radio switch
    add_switch_row(app, app->content_cont, "Wi-Fi Radio Power", &app->sw_radio, app->radio_on);

    // Query station info via CLI
    net_run_cmd("netctl sta0");
    char info_buf[512];
    net_read_output(info_buf, sizeof(info_buf));

    // Connection summary card
    lv_obj_t *scard = create_card(app->content_cont);
    lv_obj_t *stitle = lv_label_create(scard);
    if (app->sta_connected || strstr(info_buf, "RUNNING")) {
      lv_label_set_text(stitle, "Connected to Network");
      lv_obj_set_style_text_color(stitle, lv_color_hex(COL_GREEN), 0);
      app->sta_connected = true;
    } else if (strstr(info_buf, "UP")) {
      lv_label_set_text(stitle, "Station Interface UP (Idle)");
      lv_obj_set_style_text_color(stitle, lv_color_hex(COL_AMBER), 0);
    } else {
      lv_label_set_text(stitle, "Station Interface DOWN");
      lv_obj_set_style_text_color(stitle, lv_color_hex(COL_TEXT_SECOND), 0);
    }

    // Show raw interface info
    lv_obj_t *sdet = lv_label_create(scard);
    // Extract IP line from output
    char *inet_line = strstr(info_buf, "inet ");
    char *status_line = strstr(info_buf, "status:");
    char det[256];
    if (inet_line) {
      char *nl = strchr(inet_line, '\n');
      if (nl) *nl = '\0';
      snprintf(det, sizeof(det), "%s%s%s",
               inet_line,
               status_line ? "\n" : "",
               status_line ? status_line : "");
    } else if (status_line) {
      snprintf(det, sizeof(det), "%s", status_line);
    } else {
      snprintf(det, sizeof(det), "No active connection");
    }
    lv_label_set_text(sdet, det);
    lv_obj_set_style_text_color(sdet, lv_color_hex(COL_TEXT_SECOND), 0);

    // Action buttons
    add_button_row(app, app->content_cont, "[ Scan Wi-Fi Networks ]", "", COL_ACCENT_BLUE);
    add_button_row(app, app->content_cont, "[ Disconnect ]", "", COL_RED_DARK);

    // Discovered networks
    if (app->ap_count > 0) {
      lv_obj_t *lhdr = lv_label_create(app->content_cont);
      lv_label_set_text(lhdr, "Available Networks:");
      lv_obj_set_style_text_color(lhdr, lv_color_hex(COL_ACCENT_SKY), 0);

      for (int i = 0; i < app->ap_count; i++) {
        char badge[32];
        const char *sec = eos_net_wifi_auth_name(app->aps[i].authmode);
        snprintf(badge, sizeof(badge), "%d dBm Ch%u %s",
                 (int)app->aps[i].rssi, (unsigned)app->aps[i].channel, sec);
        add_button_row(app, app->content_cont, app->aps[i].ssid, badge, COL_BG_CARD);
      }
    }
  }

  // =========================================================================
  // PAGE 2: Access Point (SoftAP)
  // =========================================================================
  else if (app->cur_tab == TAB_AP) {
    // Query AP state via CLI
    net_run_cmd("netctl ap0");
    char info_buf[512];
    net_read_output(info_buf, sizeof(info_buf));
    app->ap_on = (strstr(info_buf, "UP") != NULL);

    // Row 0: SoftAP master switch
    add_switch_row(app, app->content_cont, "SoftAP Hotspot", &app->sw_ap, app->ap_on);

    // Hotspot configuration card
    lv_obj_t *acard = create_card(app->content_cont);
    lv_obj_t *atitle = lv_label_create(acard);
    lv_label_set_text(atitle, "Hotspot Settings");
    lv_obj_set_style_text_color(atitle, lv_color_hex(COL_ACCENT_SKY), 0);

    lv_obj_t *adet = lv_label_create(acard);
    char *inet_line = strstr(info_buf, "inet ");
    char ap_ip[32] = "192.168.4.1";
    if (inet_line) {
      sscanf(inet_line, "inet %31s", ap_ip);
    }
    char adet_str[256];
    snprintf(adet_str, sizeof(adet_str),
             "SSID: EOS-Hotspot  (WPA2-PSK)\nPassword: eos12345678  Channel: 1\nIP: %s (DHCP Server)",
             ap_ip);
    lv_label_set_text(adet, adet_str);
    lv_obj_set_style_text_color(adet, lv_color_hex(COL_TEXT_SECOND), 0);

    add_button_row(app, app->content_cont, "[ Apply Hotspot Settings ]", "Ch 1, WPA2", COL_ACCENT_BLUE);

    // Connected clients card
    net_run_cmd("netctl ap0 clients");
    char clients_buf[512];
    net_read_output(clients_buf, sizeof(clients_buf));

    lv_obj_t *ccard = create_card(app->content_cont);
    lv_obj_t *ctitle = lv_label_create(ccard);

    char *count_str = strstr(clients_buf, "connected station");
    if (count_str) {
      lv_label_set_text(ctitle, "Connected Clients");
      lv_obj_set_style_text_color(ctitle, lv_color_hex(COL_ACCENT_SKY), 0);
      lv_obj_t *cdesc = lv_label_create(ccard);
      lv_label_set_text(cdesc, clients_buf);
      lv_obj_set_style_text_color(cdesc, lv_color_hex(COL_TEXT_SECOND), 0);
    } else {
      lv_label_set_text(ctitle, "Connected Clients: 0");
      lv_obj_set_style_text_color(ctitle, lv_color_hex(COL_ACCENT_SKY), 0);
      lv_obj_t *clbl = lv_label_create(ccard);
      lv_label_set_text(clbl, "No client stations connected.");
      lv_obj_set_style_text_color(clbl, lv_color_hex(COL_TEXT_DIM), 0);
    }
  }

  // =========================================================================
  // PAGE 3: Bridge & Routing
  // =========================================================================
  else if (app->cur_tab == TAB_BRIDGE) {
    app->napt_on = get_napt_on();
    add_switch_row(app, app->content_cont, "NAPT Routing (AP -> STA)", &app->sw_napt, app->napt_on);

    lv_obj_t *bcard = create_card(app->content_cont);
    lv_obj_t *btitle = lv_label_create(bcard);
    lv_label_set_text(btitle, "Network Bridge Architecture");
    lv_obj_set_style_text_color(btitle, lv_color_hex(COL_ACCENT_SKY), 0);

    lv_obj_t *bdesc = lv_label_create(bcard);
    lv_label_set_text(bdesc,
                      "Bridge /dev/br0 routes network packets between\n"
                      "the SoftAP (ap0) and Station interface (sta0).\n\n"
                      "When NAPT is enabled, devices connected to the\n"
                      "hotspot can access the Internet through the\n"
                      "active Wi-Fi connection.");
    lv_obj_set_style_text_color(bdesc, lv_color_hex(COL_TEXT_SECOND), 0);

    // Conflict warnings displayed inline
    if (app->napt_on && !app->sta_connected) {
      lv_obj_t *warn = lv_label_create(bcard);
      lv_label_set_text(warn, "! Station not connected - no uplink");
      lv_obj_set_style_text_color(warn, lv_color_hex(COL_AMBER), 0);
    }
    if (app->napt_on && !app->ap_on) {
      lv_obj_t *warn = lv_label_create(bcard);
      lv_label_set_text(warn, "! Hotspot not active - no clients");
      lv_obj_set_style_text_color(warn, lv_color_hex(COL_AMBER), 0);
    }
  }

  // =========================================================================
  // PAGE 4: Interfaces
  // =========================================================================
  else if (app->cur_tab == TAB_IFACES) {
    // Hardware controller overview card
    lv_obj_t *hcard = create_card(app->content_cont);
    lv_obj_t *htitle = lv_label_create(hcard);
    lv_label_set_text(htitle, "Hardware Controller");
    lv_obj_set_style_text_color(htitle, lv_color_hex(COL_ACCENT_SKY), 0);

    // Query wifi0 status for controller info
    net_run_cmd("netctl wifi0");
    char wifi_buf[512];
    net_read_output(wifi_buf, sizeof(wifi_buf));

    lv_obj_t *hdesc = lv_label_create(hcard);
    if (strstr(wifi_buf, "ext C6") || strstr(wifi_buf, "external")) {
      lv_label_set_text(hdesc, "Target: JC4880P443C (ESP32-P4)\nController: External ESP32-C6 (SDIO)");
    } else {
      lv_label_set_text(hdesc, "Target: Lilka v2.3\nController: ESP32-S3 Internal Wi-Fi PHY");
    }
    lv_obj_set_style_text_color(hdesc, lv_color_hex(COL_TEXT_SECOND), 0);

    // Query all interfaces via CLI
    net_run_cmd("netctl");
    char all_buf[2048];
    net_read_output(all_buf, sizeof(all_buf));

    // Display each interface as a card
    const char *devs[] = {"wifi0", "sta0", "ap0", "br0"};
    for (int d = 0; d < 4; d++) {
      // Find this interface in the combined output
      char *iface_start = strstr(all_buf, devs[d]);
      if (!iface_start) continue;

      lv_obj_t *card = create_card(app->content_cont);
      lv_obj_t *title = lv_label_create(card);

      // Extract the line for this interface
      char iface_text[512] = {0};
      char *p = iface_start;
      // Collect until next interface or end
      char *next_if = NULL;
      for (int nd = d + 1; nd < 4; nd++) {
        next_if = strstr(p + 1, devs[nd]);
        if (next_if) break;
      }
      size_t copy_len = next_if ? (size_t)(next_if - iface_start) : strlen(iface_start);
      if (copy_len >= sizeof(iface_text)) copy_len = sizeof(iface_text) - 1;
      strncpy(iface_text, iface_start, copy_len);
      iface_text[copy_len] = '\0';

      // Trim trailing newlines
      while (copy_len > 0 && (iface_text[copy_len - 1] == '\n' || iface_text[copy_len - 1] == ' ')) {
        iface_text[--copy_len] = '\0';
      }

      bool is_up = (strstr(iface_text, "UP") != NULL);
      char tbuf[32];
      snprintf(tbuf, sizeof(tbuf), "%s", devs[d]);
      lv_label_set_text(title, tbuf);
      lv_obj_set_style_text_color(title, is_up ? lv_color_hex(COL_GREEN) : lv_color_hex(COL_TEXT_SECOND), 0);

      lv_obj_t *desc = lv_label_create(card);
      lv_label_set_text(desc, iface_text);
      lv_obj_set_style_text_color(desc, lv_color_hex(COL_TEXT_SECOND), 0);
    }
  }

  // =========================================================================
  // PAGE 5: Sniffer
  // =========================================================================
  else if (app->cur_tab == TAB_SNIFFER) {
    add_switch_row(app, app->content_cont, "802.11 Monitor Mode", &app->sw_promisc, app->promisc_on);

    lv_obj_t *scard = create_card(app->content_cont);
    lv_obj_t *stitle = lv_label_create(scard);
    lv_label_set_text(stitle, "Raw 802.11 Packet Sniffer");
    lv_obj_set_style_text_color(stitle, lv_color_hex(COL_ACCENT_SKY), 0);

    lv_obj_t *sdesc = lv_label_create(scard);
    lv_label_set_text(sdesc,
                      "Promiscuous mode captures raw 802.11 MAC\n"
                      "management, control, and data frames from\n"
                      "/dev/wifi0 via read() and injects via write().");
    lv_obj_set_style_text_color(sdesc, lv_color_hex(COL_TEXT_SECOND), 0);

    // Conflict warnings
    if (app->promisc_on) {
      if (app->sta_connected) {
        lv_obj_t *warn = lv_label_create(scard);
        lv_label_set_text(warn, "! Wi-Fi connection may be disrupted");
        lv_obj_set_style_text_color(warn, lv_color_hex(COL_AMBER), 0);
      }
      if (app->ap_on) {
        lv_obj_t *warn = lv_label_create(scard);
        lv_label_set_text(warn, "! Hotspot clients may be affected");
        lv_obj_set_style_text_color(warn, lv_color_hex(COL_AMBER), 0);
      }
    }
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
      lv_obj_set_style_border_color(btn, lv_color_hex(COL_ACCENT_LTBLUE), 0);
      lv_obj_set_style_border_width(btn, 2, 0);
      lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
    } else {
      lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
      lv_obj_set_style_border_width(btn, 1, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// Hardware Key Handler (Lilka Gamepad / D-Pad / Buttons)
// ---------------------------------------------------------------------------

static void net_handle_key(net_app_t *app, uint16_t key) {
  // If password dialog is active, route keys to the textarea
  if (app->pw_dialog_active) {
    switch (key) {
      case KEY_ESC:
      case KEY_B:
        pw_dialog_close(app);
        break;
      case KEY_ENTER:
      case KEY_A:
        if (app->pw_textarea) {
          const char *pass = lv_textarea_get_text(app->pw_textarea);
          int idx = app->pw_dialog_ap_idx;
          pw_dialog_close(app);
          if (idx >= 0 && idx < app->ap_count) {
            do_connect_ap(app, app->aps[idx].ssid, pass);
          }
        }
        break;
      default:
        // Let LVGL handle alphanumeric keys for the textarea
        break;
    }
    return;
  }

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
  app->radio_on = get_radio_power();
  app->sta_connected = get_sta_connected();

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  // Root screen
  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG_DEEP), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Top Header Bar (height 24, matches launcher.c)
  app->hdr = lv_obj_create(scr);
  lv_obj_set_size(app->hdr, (int32_t)w, 24);
  lv_obj_align(app->hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(app->hdr, lv_color_hex(COL_HDR_BG), 0);
  lv_obj_set_style_bg_opa(app->hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->hdr, 0, 0);
  lv_obj_set_style_radius(app->hdr, 0, 0);
  lv_obj_set_style_pad_hor(app->hdr, 6, 0);
  lv_obj_set_style_pad_ver(app->hdr, 2, 0);
  lv_obj_clear_flag(app->hdr, LV_OBJ_FLAG_SCROLLABLE);

  app->hdr_title = lv_label_create(app->hdr);
  lv_label_set_text(app->hdr_title, "EOS Network");
  lv_obj_set_style_text_color(app->hdr_title, lv_color_hex(COL_WHITE), 0);
  lv_obj_align(app->hdr_title, LV_ALIGN_LEFT_MID, 0, 0);

  // Top-right back button
  eos_lvgl_create_back_btn(scr, NULL, NULL);

  // 2. Tab Selector Bar (height 24, at y=24, matches launcher.c)
  app->tab_bar = lv_obj_create(scr);
  lv_obj_set_size(app->tab_bar, (int32_t)w, 24);
  lv_obj_align(app->tab_bar, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_set_style_bg_color(app->tab_bar, lv_color_hex(COL_TAB_BG), 0);
  lv_obj_set_style_bg_opa(app->tab_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->tab_bar, 0, 0);
  lv_obj_set_style_radius(app->tab_bar, 0, 0);
  lv_obj_set_style_pad_all(app->tab_bar, 2, 0);
  lv_obj_clear_flag(app->tab_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(app->tab_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(app->tab_bar, tab_bar_click_event_cb, LV_EVENT_CLICKED, app);

  app->tab_label = lv_label_create(app->tab_bar);
  lv_obj_set_style_text_color(app->tab_label, lv_color_hex(COL_YELLOW), 0);
  lv_obj_center(app->tab_label);
  lv_obj_clear_flag(app->tab_label, LV_OBJ_FLAG_CLICKABLE);

  // 3. Toast notification bar (hidden by default, below tab bar)
  app->toast_bar = lv_obj_create(scr);
  lv_obj_set_size(app->toast_bar, (int32_t)w - 6, 20);
  lv_obj_align(app->toast_bar, LV_ALIGN_TOP_MID, 0, 49);
  lv_obj_set_style_bg_color(app->toast_bar, lv_color_hex(COL_AMBER), 0);
  lv_obj_set_style_bg_opa(app->toast_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(app->toast_bar, 0, 0);
  lv_obj_set_style_radius(app->toast_bar, 3, 0);
  lv_obj_set_style_pad_all(app->toast_bar, 2, 0);
  lv_obj_clear_flag(app->toast_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(app->toast_bar, LV_OBJ_FLAG_HIDDEN);

  app->toast_lbl = lv_label_create(app->toast_bar);
  lv_obj_set_style_text_color(app->toast_lbl, lv_color_hex(COL_BG_DEEP), 0);
  lv_obj_align(app->toast_lbl, LV_ALIGN_LEFT_MID, 2, 0);

  // 4. Main Content Container
  int32_t content_y = 50;
  int32_t content_h = (int32_t)h - content_y - 24 - 4;
  if (content_h < 80) content_h = 80;

  app->content_cont = lv_obj_create(scr);
  lv_obj_set_size(app->content_cont, (int32_t)w - 6, content_h);
  lv_obj_align(app->content_cont, LV_ALIGN_TOP_MID, 0, content_y);
  lv_obj_set_style_bg_color(app->content_cont, lv_color_hex(COL_BG_PANEL), 0);
  lv_obj_set_style_bg_opa(app->content_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(app->content_cont, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(app->content_cont, 1, 0);
  lv_obj_set_style_radius(app->content_cont, 4, 0);
  lv_obj_set_style_pad_all(app->content_cont, 4, 0);
  lv_obj_set_style_pad_row(app->content_cont, 4, 0);
  lv_obj_set_flex_flow(app->content_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(app->content_cont, LV_DIR_VER);

  // 5. Footer Bar (height 22, at bottom, matches launcher.c)
  app->footer = lv_obj_create(scr);
  lv_obj_set_size(app->footer, (int32_t)w - 6, 22);
  lv_obj_align(app->footer, LV_ALIGN_BOTTOM_MID, 0, -1);
  lv_obj_set_style_bg_color(app->footer, lv_color_hex(COL_BG_PANEL), 0);
  lv_obj_set_style_bg_opa(app->footer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(app->footer, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(app->footer, 1, 0);
  lv_obj_set_style_radius(app->footer, 3, 0);
  lv_obj_set_style_pad_all(app->footer, 2, 0);
  lv_obj_clear_flag(app->footer, LV_OBJ_FLAG_SCROLLABLE);

  app->footer_lbl = lv_label_create(app->footer);
  bool has_kbd = (eos_lvgl_get_indev() != NULL);
  if (has_kbd) {
    lv_label_set_text(app->footer_lbl, "U/D:Item L/R:Tab A:Action B:Exit");
  } else {
    lv_label_set_text(app->footer_lbl, "Tap items | Tap tab bar for tabs");
  }
  lv_obj_set_style_text_color(app->footer_lbl, lv_color_hex(COL_TEXT_SECOND), 0);
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

    // Periodic state refresh
    uint64_t now = net_get_ms();
    if (now - app->last_refresh_ms >= 2000ULL) {
      app->last_refresh_ms = now;
      bool r = get_radio_power();
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

    // Update toast display
    update_toast(app);

    eos_lvgl_step(10);
  }

  // Disable sniffer on exit via CLI
  if (app->promisc_on) {
    net_run_cmd_silent("wlanctl sniffer off");
  }

  unlink(NET_TMP_FILE);

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
