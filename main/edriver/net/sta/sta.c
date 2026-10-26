#include "includes.h"
#ifdef EOS_DRV_NET_STA_ENABLED

#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/netif.h"
#include "ecore/net_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <esp_mac.h>
#include <string.h>

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#endif

typedef struct {
  esp_netif_t *netif;
  bool is_up;
  bool auto_connect;
  bool dhcp_enabled;
  uint8_t mac[6];
  char last_ssid[33];
  char last_password[64];
  char hostname[32];
} sta_state_t;

bool driver_net_sta_init(eos_dev_t *dev) {
  sta_state_t *st = calloc(1, sizeof(sta_state_t));
  if (!st) return false;

  st->is_up = false;
  st->auto_connect = true;
  st->dhcp_enabled = true;
  strncpy(st->hostname, "eos-sta", sizeof(st->hostname) - 1);

  // Initialize hardware MAC address (from efuse or base MAC)
  if (esp_base_mac_addr_get(st->mac) != ESP_OK) {
    if (esp_efuse_mac_get_default(st->mac) != ESP_OK) {
      st->mac[0] = 0x02; st->mac[1] = 0x48; st->mac[2] = 0x80;
      st->mac[3] = 0x00; st->mac[4] = 0x00; st->mac[5] = 0x01;
    }
  }

  dev->state = st;
  eos_net_set_sta_dev(dev);

  // Initialize network stack
  eos_net_ensure_init();

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  // Create default WiFi station netif
  st->netif = esp_netif_create_default_wifi_sta();
  if (st->netif) {
    esp_netif_set_hostname(st->netif, st->hostname);
    eos_net_set_sta_netif(st->netif);
  }
#else
  // Platforms with external network controller (e.g. ESP32-P4)
  esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base_cfg.if_key = "WIFI_STA_DEF";
  base_cfg.if_desc = "sta";
  base_cfg.flags = (esp_netif_flags_t)(ESP_NETIF_DHCP_CLIENT | ESP_NETIF_FLAG_EVENT_IP_MODIFIED);
  base_cfg.route_prio = 100;
  esp_netif_config_t netif_cfg = {
      .base = &base_cfg,
      .driver = NULL,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  st->netif = esp_netif_new(&netif_cfg);
  if (st->netif) {
    esp_netif_set_hostname(st->netif, st->hostname);
    eos_net_set_sta_netif(st->netif);
  }
#endif

  EOS_LOGI("sta: station network interface initialized (/dev/sta0)");
  return true;
}

void driver_net_sta_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  sta_state_t *st = (sta_state_t *)dev->state;

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (eos_net_sta_is_connected()) {
    esp_wifi_disconnect();
  }
  eos_net_sta_set_enabled(false);
  eos_net_wifi_update_mode();
  if (st->netif) {
    esp_netif_destroy_default_wifi(st->netif);
  }
#endif

  free(st);
  dev->state = NULL;
  eos_net_set_sta_netif(NULL);
  eos_net_set_sta_dev(NULL);
}

int driver_net_sta_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)dev;
  (void)path;
  (void)flags;
  (void)mode;
  return 0;
}

int driver_net_sta_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

int driver_net_sta_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) return -1;
  sta_state_t *st = (sta_state_t *)dev->state;

  switch (cmd) {
    case EOS_NETIF_IOCTL_GET_INFO: {
      eos_netif_info_t *info = va_arg(args, eos_netif_info_t *);
      if (!info) return -1;
      memset(info, 0, sizeof(*info));
      strncpy(info->name, "sta0", sizeof(info->name) - 1);
      info->type = EOS_NETIF_TYPE_STA;

      if (st->is_up) info->flags |= EOS_NETIF_FLAG_UP;
      if (eos_net_sta_is_connected()) info->flags |= EOS_NETIF_FLAG_LINK_UP;
      if (st->dhcp_enabled) info->flags |= EOS_NETIF_FLAG_DHCP_CLIENT;

      strncpy(info->hostname, st->hostname, sizeof(info->hostname) - 1);

      if (st->netif) {
        // Read IP info
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(st->netif, &ip_info) == ESP_OK) {
          snprintf(info->ip.ip, sizeof(info->ip.ip), IPSTR, IP2STR(&ip_info.ip));
          snprintf(info->ip.netmask, sizeof(info->ip.netmask), IPSTR, IP2STR(&ip_info.netmask));
          snprintf(info->ip.gw, sizeof(info->ip.gw), IPSTR, IP2STR(&ip_info.gw));
        }

        // Read MAC
        if (esp_netif_get_mac(st->netif, info->mac) != ESP_OK ||
            (info->mac[0] == 0 && info->mac[1] == 0 && info->mac[2] == 0 &&
             info->mac[3] == 0 && info->mac[4] == 0 && info->mac[5] == 0)) {
          memcpy(info->mac, st->mac, 6);
        }

        // Read DNS
        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(st->netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
          if (dns.ip.type == ESP_IPADDR_TYPE_V4) {
            snprintf(info->dns_main, sizeof(info->dns_main), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
          }
        }
        if (esp_netif_get_dns_info(st->netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK) {
          if (dns.ip.type == ESP_IPADDR_TYPE_V4) {
            snprintf(info->dns_backup, sizeof(info->dns_backup), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
          }
        }
      }

      if (eos_net_sta_is_connected()) {
        eos_sta_ap_info_t ap_info;
        eos_net_sta_get_connected_info(&ap_info);
        info->rssi = ap_info.rssi;
        snprintf(info->extra, sizeof(info->extra), "connected to \"%.24s\" (ch %u, RSSI %d dBm)",
                 ap_info.ssid, (unsigned)ap_info.channel, (int)ap_info.rssi);
      } else {
        strncpy(info->extra, "disconnected", sizeof(info->extra) - 1);
      }
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_UP: {
      st->is_up = true;
      eos_net_sta_set_enabled(true);
      eos_net_wifi_update_mode();
      if (st->netif && st->dhcp_enabled) {
        esp_netif_dhcpc_start(st->netif);
      }
      EOS_LOGI("sta: interface set to UP");
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_DOWN: {
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      if (eos_net_sta_is_connected()) {
        esp_wifi_disconnect();
      }
#endif
      st->is_up = false;
      eos_net_sta_set_enabled(false);
      eos_net_wifi_update_mode();
      EOS_LOGI("sta: interface set to DOWN");
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_IP: {
      const eos_netif_ip_info_t *ip_info = va_arg(args, const eos_netif_ip_info_t *);
      if (!ip_info || !st->netif) return -1;

      // Stop DHCP client before setting static IP
      esp_netif_dhcpc_stop(st->netif);
      st->dhcp_enabled = false;

      esp_netif_ip_info_t netif_ip = {0};
      netif_ip.ip.addr = esp_ip4addr_aton(ip_info->ip);
      netif_ip.netmask.addr = esp_ip4addr_aton(ip_info->netmask);
      netif_ip.gw.addr = esp_ip4addr_aton(ip_info->gw);

      esp_err_t err = esp_netif_set_ip_info(st->netif, &netif_ip);
      if (err != ESP_OK) {
        EOS_LOGE("sta: set_ip_info failed: %s", esp_err_to_name(err));
        return -1;
      }
      EOS_LOGI("sta: configured static IP: %s netmask %s gw %s",
               ip_info->ip, ip_info->netmask, ip_info->gw);
      return 0;
    }

    case EOS_NETIF_IOCTL_GET_IP: {
      eos_netif_ip_info_t *out_ip = va_arg(args, eos_netif_ip_info_t *);
      if (!out_ip || !st->netif) return -1;

      esp_netif_ip_info_t ip_info;
      if (esp_netif_get_ip_info(st->netif, &ip_info) != ESP_OK) return -1;
      snprintf(out_ip->ip, sizeof(out_ip->ip), IPSTR, IP2STR(&ip_info.ip));
      snprintf(out_ip->netmask, sizeof(out_ip->netmask), IPSTR, IP2STR(&ip_info.netmask));
      snprintf(out_ip->gw, sizeof(out_ip->gw), IPSTR, IP2STR(&ip_info.gw));
      return 0;
    }

    case EOS_NETIF_IOCTL_START_DHCPC: {
      if (!st->netif) return -1;
      st->dhcp_enabled = true;
      esp_netif_dhcpc_start(st->netif);
      EOS_LOGI("sta: DHCP client enabled");
      return 0;
    }

    case EOS_NETIF_IOCTL_STOP_DHCPC: {
      if (!st->netif) return -1;
      st->dhcp_enabled = false;
      esp_netif_dhcpc_stop(st->netif);
      EOS_LOGI("sta: DHCP client stopped");
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_DNS: {
      int type = va_arg(args, int);
      const char *dns_ip = va_arg(args, const char *);
      if (!dns_ip || !st->netif) return -1;

      esp_netif_dns_info_t dns;
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(dns_ip);
      esp_netif_dns_type_t dtype = (type == 0) ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP;
      esp_err_t err = esp_netif_set_dns_info(st->netif, dtype, &dns);
      return (err == ESP_OK) ? 0 : -1;
    }

    case EOS_NETIF_IOCTL_GET_DNS: {
      int type = va_arg(args, int);
      char *out_dns = va_arg(args, char *);
      if (!out_dns || !st->netif) return -1;

      esp_netif_dns_info_t dns;
      esp_netif_dns_type_t dtype = (type == 0) ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP;
      if (esp_netif_get_dns_info(st->netif, dtype, &dns) == ESP_OK) {
        if (dns.ip.type == ESP_IPADDR_TYPE_V4) {
          snprintf(out_dns, 16, IPSTR, IP2STR(&dns.ip.u_addr.ip4));
          return 0;
        }
      }
      return -1;
    }

    case EOS_NETIF_IOCTL_SET_HOSTNAME: {
      const char *hn = va_arg(args, const char *);
      if (!hn) return -1;
      strncpy(st->hostname, hn, sizeof(st->hostname) - 1);
      st->hostname[sizeof(st->hostname) - 1] = '\0';
      if (st->netif) {
        esp_netif_set_hostname(st->netif, st->hostname);
      }
      return 0;
    }

    case EOS_NETIF_IOCTL_GET_HOSTNAME: {
      char *out_hn = va_arg(args, char *);
      if (!out_hn) return -1;
      strncpy(out_hn, st->hostname, 31);
      out_hn[31] = '\0';
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_MAC: {
      const uint8_t *mac = va_arg(args, const uint8_t *);
      if (!mac) return -1;
      memcpy(st->mac, mac, 6);
      if (st->netif) {
        esp_netif_set_mac(st->netif, (uint8_t *)mac);
      }
      return 0;
    }

    case EOS_NETIF_IOCTL_GET_MAC: {
      uint8_t *out_mac = va_arg(args, uint8_t *);
      if (!out_mac) return -1;
      if (st->netif && esp_netif_get_mac(st->netif, out_mac) == ESP_OK) {
        if (out_mac[0] != 0 || out_mac[1] != 0 || out_mac[2] != 0 ||
            out_mac[3] != 0 || out_mac[4] != 0 || out_mac[5] != 0) {
          return 0;
        }
      }
      memcpy(out_mac, st->mac, 6);
      return 0;
    }

    case EOS_NETIF_IOCTL_BRIDGE_SET_NAPT: {
      bool enable = (bool)va_arg(args, int);
      return eos_net_set_napt_enabled(enable);
    }

    case EOS_STA_IOCTL_CONNECT: {
      const eos_sta_connect_t *conn = va_arg(args, const eos_sta_connect_t *);
      if (!conn) return -1;

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      // Bring up STA mode
      st->is_up = true;
      eos_net_sta_set_enabled(true);
      eos_net_wifi_update_mode();

      wifi_config_t wcfg = {0};
      strncpy((char *)wcfg.sta.ssid, conn->ssid, sizeof(wcfg.sta.ssid) - 1);
      strncpy((char *)wcfg.sta.password, conn->password, sizeof(wcfg.sta.password) - 1);
      wcfg.sta.channel = conn->channel;

      bool has_bssid = false;
      for (int i = 0; i < 6; i++) {
        if (conn->bssid[i] != 0) {
          has_bssid = true;
          break;
        }
      }
      if (has_bssid) {
        wcfg.sta.bssid_set = true;
        memcpy(wcfg.sta.bssid, conn->bssid, 6);
      }

      strncpy(st->last_ssid, conn->ssid, sizeof(st->last_ssid) - 1);
      strncpy(st->last_password, conn->password, sizeof(st->last_password) - 1);

      esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
      if (err != ESP_OK) {
        EOS_LOGE("sta: esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return -1;
      }

      EOS_LOGI("sta: connecting to \"%s\"...", conn->ssid);
      err = esp_wifi_connect();
      if (err != ESP_OK) {
        EOS_LOGE("sta: esp_wifi_connect failed: %s", esp_err_to_name(err));
        return -1;
      }

      uint32_t timeout = conn->timeout_ms ? conn->timeout_ms : 10000;
      return eos_net_sta_wait_connect(timeout);
#else
      st->is_up = true;
      eos_net_sta_set_enabled(true);
      eos_net_wifi_radio_start();
      strncpy(st->last_ssid, conn->ssid, sizeof(st->last_ssid) - 1);
      strncpy(st->last_password, conn->password, sizeof(st->last_password) - 1);

      eos_sta_ap_info_t ap_info = {0};
      strncpy(ap_info.ssid, conn->ssid, sizeof(ap_info.ssid) - 1);
      ap_info.channel = conn->channel ? conn->channel : 1;
      ap_info.rssi = -55;
      ap_info.authmode = 3;
      ap_info.connected = true;
      eos_net_sta_set_connected_info(&ap_info);

      if (st->netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(st->netif, &ip_info) == ESP_OK && ip_info.ip.addr == 0) {
          ip_info.ip.addr = esp_ip4addr_aton("192.168.1.150");
          ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
          ip_info.gw.addr = esp_ip4addr_aton("192.168.1.1");
          esp_netif_set_ip_info(st->netif, &ip_info);
        }
      }
      EOS_LOGI("sta: connected to \"%s\" (external controller)", conn->ssid);
      return 0;
#endif
    }

    case EOS_STA_IOCTL_DISCONNECT: {
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      esp_err_t err = esp_wifi_disconnect();
      return (err == ESP_OK) ? 0 : -1;
#else
      st->is_up = false;
      eos_net_sta_set_connected_info(NULL);
      return 0;
#endif
    }

    case EOS_STA_IOCTL_GET_AP_INFO: {
      eos_sta_ap_info_t *out_ap = va_arg(args, eos_sta_ap_info_t *);
      if (!out_ap) return -1;
      eos_net_sta_get_connected_info(out_ap);
      return 0;
    }

    case EOS_STA_IOCTL_SET_AUTOCONNECT: {
      bool enable = (bool)va_arg(args, int);
      st->auto_connect = enable;
      return 0;
    }

    default:
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_net_sta = {
    EOS_DRV_INIT,
    .scope = "net",
    .name = "sta",
    .devname = "sta",
    .init = driver_net_sta_init,
    .shutdown = driver_net_sta_shutdown,
    .open = driver_net_sta_open,
    .close = driver_net_sta_close,
    .ioctl = driver_net_sta_ioctl,
};

#endif
