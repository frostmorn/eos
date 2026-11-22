#include "ebin/lvgl_net/net_logic.h"
#include "emisc/netmisc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

uint64_t net_logic_get_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * (uint64_t)EOS_MS_PER_SEC + (uint64_t)tv.tv_usec / (uint64_t)EOS_US_PER_MS;
}

static int open_iface_dev(const char *dev) {
  if (!dev || !dev[0]) return -1;
  char path[EOS_LINE_STR_LEN];
  if (dev[0] == '/') {
    snprintf(path, sizeof(path), "%s", dev);
  } else {
    snprintf(path, sizeof(path), "/dev/%s", dev);
  }
  return open(path, O_RDWR);
}

void net_logic_set_status(net_logic_t *logic, uint8_t type, const char *fmt, ...) {
  if (!logic) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(logic->status_msg, sizeof(logic->status_msg), fmt, ap);
  va_end(ap);
  logic->status_type = type;

  snprintf(logic->toast_msg, sizeof(logic->toast_msg), "%s", logic->status_msg);
  logic->toast_expire_ms = net_logic_get_ms() + NET_TOAST_DURATION_MS;
}

bool net_logic_update_toast(net_logic_t *logic) {
  if (!logic) return false;
  if (logic->toast_expire_ms > 0 && net_logic_get_ms() > logic->toast_expire_ms) {
    logic->toast_expire_ms = 0;
    return true; // Toast expired
  }
  return false;
}

bool net_logic_ioctl_get_radio_power(void) {
  int fd = open_iface_dev("wifi0");
  if (fd < 0) return false;
  int power = 0;
  int ret = ioctl(fd, EOS_RADIO_IOCTL_GET_POWER, &power);
  close(fd);
  return (ret == 0 && power != 0);
}

bool net_logic_ioctl_set_radio_power(bool on) {
  int fd = open_iface_dev("wifi0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_RADIO_IOCTL_POWER, on ? 1 : 0);
  close(fd);
  return (ret == 0);
}

bool net_logic_ioctl_get_netif_info(const char *dev, eos_netif_info_t *out_info) {
  if (!out_info) return false;
  memset(out_info, 0, sizeof(*out_info));
  int fd = open_iface_dev(dev);
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_NETIF_IOCTL_GET_INFO, out_info);
  close(fd);
  return (ret == 0);
}

bool net_logic_ioctl_get_sta_ap_info(eos_sta_ap_info_t *out_ap) {
  if (!out_ap) return false;
  memset(out_ap, 0, sizeof(*out_ap));
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_STA_IOCTL_GET_AP_INFO, out_ap);
  close(fd);
  return (ret == 0);
}

bool net_logic_ioctl_get_ap_config(eos_ap_config_t *out_cfg) {
  if (!out_cfg) return false;
  memset(out_cfg, 0, sizeof(*out_cfg));
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_AP_IOCTL_GET_CONFIG, out_cfg);
  close(fd);
  if (ret != 0 || out_cfg->ssid[0] == '\0') {
    strlcpy(out_cfg->ssid, "EOS-Hotspot", sizeof(out_cfg->ssid));
    strlcpy(out_cfg->password, "eos12345678", sizeof(out_cfg->password));
    out_cfg->channel = NET_DEFAULT_AP_CHANNEL;
    out_cfg->authmode = NET_DEFAULT_AP_AUTHMODE;
    out_cfg->max_connection = NET_DEFAULT_AP_MAX_CONN;
  }
  return true;
}

bool net_logic_ioctl_set_ap_config(const eos_ap_config_t *cfg) {
  if (!cfg) return false;
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_AP_IOCTL_SET_CONFIG, cfg);
  close(fd);
  return (ret == 0);
}

bool net_logic_ioctl_get_bridge_info(eos_netif_bridge_info_t *out_binfo) {
  if (!out_binfo) return false;
  memset(out_binfo, 0, sizeof(*out_binfo));
  int fd = open_iface_dev("br0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_GET_INFO, out_binfo);
  close(fd);
  return (ret == 0);
}

bool net_logic_ioctl_set_bridge_napt(bool on) {
  int fd = open_iface_dev("br0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_SET_NAPT, on);
  close(fd);
  return (ret == 0);
}

bool net_logic_ioctl_set_promisc(bool on) {
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

void net_logic_init(net_logic_t *logic) {
  if (!logic) return;
  memset(logic, 0, sizeof(*logic));
  logic->cur_tab = TAB_STA;
  logic->sel_idx = 0;
  net_logic_ioctl_get_ap_config(&logic->ap_cfg);
  net_logic_refresh_state(logic);
}

bool net_logic_refresh_state(net_logic_t *logic) {
  if (!logic) return false;
  logic->radio_on = net_logic_ioctl_get_radio_power();

  eos_netif_info_t sta_info;
  bool has_sta = net_logic_ioctl_get_netif_info("sta0", &sta_info);
  logic->sta_connected = has_sta && ((sta_info.flags & EOS_NETIF_FLAG_LINK_UP) != 0);
  logic->sta_dhcp_on = has_sta && ((sta_info.flags & EOS_NETIF_FLAG_DHCP_CLIENT) != 0);

  eos_netif_info_t ap_info;
  bool has_ap = net_logic_ioctl_get_netif_info("ap0", &ap_info);
  logic->ap_on = has_ap && ((ap_info.flags & EOS_NETIF_FLAG_UP) != 0);
  logic->ap_dhcp_on = has_ap && ((ap_info.flags & EOS_NETIF_FLAG_DHCP_SERVER) != 0);

  eos_netif_bridge_info_t br_info;
  bool has_br = net_logic_ioctl_get_bridge_info(&br_info);
  logic->napt_on = has_br && br_info.napt_enabled;

  return true;
}

bool net_logic_toggle_radio(net_logic_t *logic) {
  if (!logic) return false;
  bool new_state = !logic->radio_on;
  if (net_logic_ioctl_set_radio_power(new_state)) {
    logic->radio_on = new_state;
    net_logic_set_status(logic, new_state ? NET_STATUS_SUCCESS : NET_STATUS_WARN,
                         "Wi-Fi radio turned %s", new_state ? "ON" : "OFF");
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to toggle Wi-Fi radio");
  return false;
}

void net_logic_sort_aps(net_ap_item_t *aps, int count) {
  if (!aps || count <= 1) return;
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (aps[j].rssi < aps[j + 1].rssi) {
        net_ap_item_t tmp = aps[j];
        aps[j] = aps[j + 1];
        aps[j + 1] = tmp;
      }
    }
  }
}

bool net_logic_scan(net_logic_t *logic) {
  if (!logic) return false;
  int fd = open_iface_dev("wifi0");
  if (fd < 0) {
    net_logic_set_status(logic, NET_STATUS_ERR, "Wi-Fi scan failed (interface not found)");
    return false;
  }

  eos_wifi_scan_config_t scfg = {
      .mode = EOS_WIFI_SCAN_ACTIVE,
      .channel = 0,
      .show_hidden = false,
      .dwell_time_ms = NET_SCAN_DWELL_MS,
  };

  if (ioctl(fd, EOS_RADIO_IOCTL_SCAN_START, &scfg) != 0) {
    close(fd);
    net_logic_set_status(logic, NET_STATUS_ERR, "Wi-Fi scan failed (ensure radio is ON)");
    return false;
  }

  uint16_t ap_count = 0;
  ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_COUNT, &ap_count);

  if (ap_count > (uint16_t)NET_MAX_SCAN_APS) ap_count = (uint16_t)NET_MAX_SCAN_APS;

  logic->ap_count = 0;
  if (ap_count > 0) {
    eos_wifi_scan_ap_t *recs = calloc(ap_count, sizeof(eos_wifi_scan_ap_t));
    if (recs) {
      eos_wifi_scan_results_t res = {
          .aps = recs,
          .max_count = ap_count,
          .count = 0,
      };
      if (ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_RESULTS, &res) == 0) {
        logic->ap_count = (int)res.count;
        for (size_t i = 0; i < res.count; i++) {
          const char *name = recs[i].is_hidden ? "<Hidden>" : recs[i].ssid;
          strncpy(logic->aps[i].ssid, name[0] ? name : "<Hidden>", sizeof(logic->aps[i].ssid) - 1);
          logic->aps[i].rssi = recs[i].rssi;
          logic->aps[i].channel = recs[i].channel;
          logic->aps[i].authmode = recs[i].authmode;
        }
        net_logic_sort_aps(logic->aps, logic->ap_count);
      }
      free(recs);
    }
  }

  close(fd);
  logic->radio_on = true;
  net_logic_set_status(logic, NET_STATUS_SUCCESS, "Scan complete: %d network(s) found", logic->ap_count);
  return true;
}

bool net_logic_connect_ap(net_logic_t *logic, const char *ssid, const char *pass) {
  if (!logic || !ssid || !ssid[0]) return false;
  strncpy(logic->connect_ssid, ssid, sizeof(logic->connect_ssid) - 1);

  int fd = open_iface_dev("sta0");
  if (fd < 0) {
    net_logic_set_status(logic, NET_STATUS_ERR, "Station interface sta0 unavailable");
    return false;
  }
  eos_sta_connect_t conn;
  memset(&conn, 0, sizeof(conn));
  strncpy(conn.ssid, ssid, sizeof(conn.ssid) - 1);
  if (pass && pass[0]) {
    strncpy(conn.password, pass, sizeof(conn.password) - 1);
  }
  conn.timeout_ms = NET_STA_CONNECT_TIMEOUT_MS;
  int ret = ioctl(fd, EOS_STA_IOCTL_CONNECT, &conn);
  close(fd);

  if (ret == 0) {
    logic->sta_connected = true;
    net_logic_set_status(logic, NET_STATUS_SUCCESS, "Connected to \"%.24s\"", ssid);
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to connect to \"%.24s\"", ssid);
  return false;
}

bool net_logic_disconnect(net_logic_t *logic) {
  if (!logic) return false;
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_STA_IOCTL_DISCONNECT);
  close(fd);
  if (ret == 0) {
    logic->sta_connected = false;
    net_logic_set_status(logic, NET_STATUS_WARN, "Disconnected from network");
    return true;
  }
  return false;
}

bool net_logic_toggle_ap(net_logic_t *logic) {
  if (!logic) return false;
  bool new_state = !logic->ap_on;
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;

  int ret = 0;
  if (new_state) {
    ioctl(fd, EOS_AP_IOCTL_SET_CONFIG, &logic->ap_cfg);
    ret = ioctl(fd, EOS_NETIF_IOCTL_SET_UP);
  } else {
    ret = ioctl(fd, EOS_NETIF_IOCTL_SET_DOWN);
  }
  close(fd);

  if (ret == 0) {
    logic->ap_on = new_state;
    net_logic_set_status(logic, new_state ? NET_STATUS_SUCCESS : NET_STATUS_WARN,
                         "SoftAP %s (\"%s\", Ch%u)",
                         new_state ? "started" : "stopped",
                         logic->ap_cfg.ssid, (unsigned)logic->ap_cfg.channel);
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to toggle SoftAP");
  return false;
}

bool net_logic_toggle_napt(net_logic_t *logic) {
  if (!logic) return false;
  bool new_state = !logic->napt_on;
  if (net_logic_ioctl_set_bridge_napt(new_state)) {
    logic->napt_on = new_state;
    net_logic_set_status(logic, new_state ? NET_STATUS_SUCCESS : NET_STATUS_WARN,
                         "Bridge NAPT routing %s", new_state ? "enabled" : "disabled");
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to configure Bridge NAPT");
  return false;
}

bool net_logic_toggle_promisc(net_logic_t *logic) {
  if (!logic) return false;
  bool new_state = !logic->promisc_on;
  if (net_logic_ioctl_set_promisc(new_state)) {
    logic->promisc_on = new_state;
    net_logic_set_status(logic, new_state ? NET_STATUS_SUCCESS : NET_STATUS_WARN,
                         "802.11 monitor mode %s", new_state ? "enabled" : "disabled");
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to toggle monitor mode");
  return false;
}

bool net_logic_toggle_dhcpc(net_logic_t *logic) {
  if (!logic) return false;
  eos_netif_info_t info;
  if (!net_logic_ioctl_get_netif_info("sta0", &info)) return false;
  bool is_dhcp = (info.flags & EOS_NETIF_FLAG_DHCP_CLIENT) != 0;
  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  int ret = ioctl(fd, is_dhcp ? EOS_NETIF_IOCTL_STOP_DHCPC : EOS_NETIF_IOCTL_START_DHCPC);
  close(fd);
  if (ret == 0) {
    net_logic_set_status(logic, NET_STATUS_SUCCESS, "Station DHCP client toggled");
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to toggle DHCP client");
  return false;
}

bool net_logic_toggle_dhcps(net_logic_t *logic) {
  if (!logic) return false;
  eos_netif_info_t info;
  if (!net_logic_ioctl_get_netif_info("ap0", &info)) return false;
  bool is_dhcp = (info.flags & EOS_NETIF_FLAG_DHCP_SERVER) != 0;
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;
  int ret = ioctl(fd, is_dhcp ? EOS_NETIF_IOCTL_STOP_DHCPS : EOS_NETIF_IOCTL_START_DHCPS);
  close(fd);
  if (ret == 0) {
    net_logic_set_status(logic, NET_STATUS_SUCCESS, "SoftAP DHCP server toggled");
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to toggle DHCP server");
  return false;
}

bool net_logic_cycle_ap_chan(net_logic_t *logic) {
  if (!logic) return false;
  uint8_t ch = logic->ap_cfg.channel;
  if (ch == 1) ch = 6;
  else if (ch == 6) ch = 11;
  else if (ch == 11) ch = 2;
  else if (ch >= 13) ch = 1;
  else ch++;

  logic->ap_cfg.channel = ch;
  net_logic_ioctl_set_ap_config(&logic->ap_cfg);
  net_logic_set_status(logic, NET_STATUS_SUCCESS, "SoftAP channel set to Ch %u (%u MHz)",
                       (unsigned)ch,
                       (unsigned)(NET_WIFI_BASE_FREQ_MHZ + ch * NET_WIFI_CHAN_SPACING_MHZ));
  return true;
}

bool net_logic_toggle_ap_security(net_logic_t *logic) {
  if (!logic) return false;
  if (logic->ap_cfg.authmode == 0) {
    logic->ap_cfg.authmode = NET_DEFAULT_AP_AUTHMODE;
    if (strlen(logic->ap_cfg.password) < NET_MIN_PASS_LEN) {
      strlcpy(logic->ap_cfg.password, "eos12345678", sizeof(logic->ap_cfg.password));
    }
    net_logic_ioctl_set_ap_config(&logic->ap_cfg);
    net_logic_set_status(logic, NET_STATUS_SUCCESS, "SoftAP security set to WPA2-PSK (password: %s)", logic->ap_cfg.password);
  } else {
    logic->ap_cfg.authmode = 0;
    logic->ap_cfg.password[0] = '\0';
    net_logic_ioctl_set_ap_config(&logic->ap_cfg);
    net_logic_set_status(logic, NET_STATUS_SUCCESS, "SoftAP security set to Open (no encryption)");
  }
  return true;
}

bool net_logic_set_ap_ssid(net_logic_t *logic, const char *ssid) {
  if (!logic || !ssid || !ssid[0]) return false;
  strlcpy(logic->ap_cfg.ssid, ssid, sizeof(logic->ap_cfg.ssid));
  net_logic_ioctl_set_ap_config(&logic->ap_cfg);
  net_logic_set_status(logic, NET_STATUS_SUCCESS, "SoftAP SSID updated to \"%s\"", logic->ap_cfg.ssid);
  return true;
}

bool net_logic_set_ap_pass(net_logic_t *logic, const char *pass) {
  if (!logic) return false;
  if (!pass || pass[0] == '\0') {
    logic->ap_cfg.authmode = 0;
    logic->ap_cfg.password[0] = '\0';
    net_logic_ioctl_set_ap_config(&logic->ap_cfg);
    net_logic_set_status(logic, NET_STATUS_SUCCESS, "SoftAP encryption set to Open (no password)");
    return true;
  }
  if (strlen(pass) < NET_MIN_PASS_LEN) {
    net_logic_set_status(logic, NET_STATUS_WARN, "Password must be >= 8 chars or empty for Open");
    return false;
  }
  logic->ap_cfg.authmode = NET_DEFAULT_AP_AUTHMODE;
  strlcpy(logic->ap_cfg.password, pass, sizeof(logic->ap_cfg.password));
  net_logic_ioctl_set_ap_config(&logic->ap_cfg);
  net_logic_set_status(logic, NET_STATUS_SUCCESS, "SoftAP password updated (WPA2-PSK)");
  return true;
}

bool net_logic_set_sta_static_ip(net_logic_t *logic, const char *text) {
  if (!logic || !text || !text[0]) return false;
  char ip[EOS_XSMALL_STR_LEN] = {0}, mask[EOS_XSMALL_STR_LEN] = "255.255.255.0", gw[EOS_XSMALL_STR_LEN] = "192.168.1.1";
  sscanf(text, "%15s %15s %15s", ip, mask, gw);
  if (!ip[0]) return false;

  int fd = open_iface_dev("sta0");
  if (fd < 0) return false;
  eos_netif_ip_info_t ip_info;
  memset(&ip_info, 0, sizeof(ip_info));
  strlcpy(ip_info.ip, ip, sizeof(ip_info.ip));
  strlcpy(ip_info.netmask, mask, sizeof(ip_info.netmask));
  strlcpy(ip_info.gw, gw, sizeof(ip_info.gw));
  int ret = ioctl(fd, EOS_NETIF_IOCTL_SET_IP, &ip_info);
  close(fd);

  if (ret == 0) {
    net_logic_set_status(logic, NET_STATUS_SUCCESS, "Static IP configured: %s", ip);
    return true;
  }
  net_logic_set_status(logic, NET_STATUS_ERR, "Failed to set static IP");
  return false;
}

bool net_logic_toggle_iface(int idx) {
  const char *devs[NET_MAX_IFACES] = {"wifi0", "sta0", "ap0", "br0"};
  if (idx < 0 || idx >= NET_MAX_IFACES) return false;

  eos_netif_info_t info;
  if (!net_logic_ioctl_get_netif_info(devs[idx], &info)) return false;
  bool is_up = (info.flags & EOS_NETIF_FLAG_UP) != 0;
  int fd = open_iface_dev(devs[idx]);
  if (fd < 0) return false;
  int ret = ioctl(fd, is_up ? EOS_NETIF_IOCTL_SET_DOWN : EOS_NETIF_IOCTL_SET_UP);
  close(fd);
  return (ret == 0);
}

bool net_logic_get_ap_stas(eos_ap_sta_list_t *stas) {
  if (!stas) return false;
  memset(stas, 0, sizeof(*stas));
  int fd = open_iface_dev("ap0");
  if (fd < 0) return false;
  int ret = ioctl(fd, EOS_AP_IOCTL_GET_STAS, stas);
  close(fd);
  return (ret == 0);
}
