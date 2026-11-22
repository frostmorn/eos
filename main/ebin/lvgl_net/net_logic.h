#pragma once

#include "ecore/ioctl.h"
#include "ecore/netif.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define NET_MAX_SCAN_APS 16
#define NET_NUM_TABS     5
#define NET_MAX_ITEMS    48
#define NET_MAX_IFACES   4

#define NET_SSID_MAX_LEN          33
#define NET_SCAN_DWELL_MS         120
#define NET_MIN_CONTENT_H         60
#define NET_STA_CONNECT_TIMEOUT_MS 8000
#define NET_TOAST_DURATION_MS     3500ULL
#define NET_REFRESH_INTERVAL_MS   3000ULL

#define NET_DEFAULT_AP_CHANNEL    1
#define NET_DEFAULT_AP_AUTHMODE   3
#define NET_DEFAULT_AP_MAX_CONN   4
#define NET_WIFI_BASE_FREQ_MHZ    2407
#define NET_WIFI_CHAN_SPACING_MHZ 5
#define NET_MIN_PASS_LEN          8

#define NET_STATUS_INFO    0
#define NET_STATUS_WARN    1
#define NET_STATUS_ERR     2
#define NET_STATUS_SUCCESS 3

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
  char ssid[NET_SSID_MAX_LEN];
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
  char connect_ssid[NET_SSID_MAX_LEN];
  char status_msg[EOS_BUFFER_128_LEN];
  uint8_t status_type;

  // Modal Dialog state
  bool dialog_active;
  net_dialog_type_t dialog_type;
  int dialog_ap_idx;
  char dialog_buf[EOS_LINE_STR_LEN];

  // Toast notification state
  char toast_msg[EOS_BUFFER_128_LEN];
  uint64_t toast_expire_ms;

  uint64_t last_refresh_ms;
} net_logic_t;

uint64_t net_logic_get_ms(void);
void net_logic_init(net_logic_t *logic);
void net_logic_set_status(net_logic_t *logic, uint8_t type, const char *fmt, ...);
bool net_logic_update_toast(net_logic_t *logic);
bool net_logic_refresh_state(net_logic_t *logic);
bool net_logic_toggle_radio(net_logic_t *logic);
bool net_logic_scan(net_logic_t *logic);
bool net_logic_connect_ap(net_logic_t *logic, const char *ssid, const char *pass);
bool net_logic_disconnect(net_logic_t *logic);
bool net_logic_toggle_ap(net_logic_t *logic);
bool net_logic_toggle_napt(net_logic_t *logic);
bool net_logic_toggle_promisc(net_logic_t *logic);
bool net_logic_toggle_dhcpc(net_logic_t *logic);
bool net_logic_toggle_dhcps(net_logic_t *logic);
bool net_logic_cycle_ap_chan(net_logic_t *logic);
bool net_logic_toggle_ap_security(net_logic_t *logic);
bool net_logic_set_ap_ssid(net_logic_t *logic, const char *ssid);
bool net_logic_set_ap_pass(net_logic_t *logic, const char *pass);
bool net_logic_set_sta_static_ip(net_logic_t *logic, const char *text);
bool net_logic_toggle_iface(int idx);
bool net_logic_get_ap_stas(eos_ap_sta_list_t *stas);
void net_logic_sort_aps(net_ap_item_t *aps, int count);

// Direct IOCTL wrappers
bool net_logic_ioctl_get_radio_power(void);
bool net_logic_ioctl_set_radio_power(bool on);
bool net_logic_ioctl_get_netif_info(const char *dev, eos_netif_info_t *out_info);
bool net_logic_ioctl_get_sta_ap_info(eos_sta_ap_info_t *out_ap);
bool net_logic_ioctl_get_ap_config(eos_ap_config_t *out_cfg);
bool net_logic_ioctl_set_ap_config(const eos_ap_config_t *cfg);
bool net_logic_ioctl_get_bridge_info(eos_netif_bridge_info_t *out_binfo);
bool net_logic_ioctl_set_bridge_napt(bool on);
bool net_logic_ioctl_set_promisc(bool on);
