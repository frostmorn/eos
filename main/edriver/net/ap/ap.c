#include "includes.h"
#ifdef EOS_DRV_NET_AP_ENABLED

#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/netif.h"
#include "edriver/net/net_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#endif

typedef struct {
  esp_netif_t *netif;
  bool is_up;
  bool dhcps_enabled;
  eos_ap_config_t ap_cfg;
  char hostname[32];
} ap_state_t;

bool driver_net_ap_init(eos_dev_t *dev) {
  ap_state_t *st = calloc(1, sizeof(ap_state_t));
  if (!st) return false;

  st->is_up = false;
  st->dhcps_enabled = true;
  strncpy(st->hostname, "eos-ap", sizeof(st->hostname) - 1);

  // Default SoftAP configuration
  strncpy(st->ap_cfg.ssid, "EOS-AP", sizeof(st->ap_cfg.ssid) - 1);
  strncpy(st->ap_cfg.password, "12345678", sizeof(st->ap_cfg.password) - 1);
  st->ap_cfg.channel = 1;
  st->ap_cfg.max_connection = 4;
  st->ap_cfg.authmode = 3; // WPA2_PSK
  st->ap_cfg.ssid_hidden = false;

  dev->state = st;
  eos_net_set_ap_dev(dev);

  // Initialize network stack
  eos_net_ensure_init();

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  // Create default WiFi softAP netif
  st->netif = esp_netif_create_default_wifi_ap();
  if (st->netif) {
    esp_netif_set_hostname(st->netif, st->hostname);
    eos_net_set_ap_netif(st->netif);
  }
#else
  esp_netif_ip_info_t ap_ip_info = {0};
  ap_ip_info.ip.addr = esp_ip4addr_aton("192.168.4.1");
  ap_ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
  ap_ip_info.gw.addr = esp_ip4addr_aton("192.168.4.1");

  esp_netif_inherent_config_t ap_base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
  ap_base_cfg.if_key = "WIFI_AP_DEF";
  ap_base_cfg.if_desc = "ap";
  ap_base_cfg.flags = (esp_netif_flags_t)(ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP);
  ap_base_cfg.ip_info = &ap_ip_info;
  ap_base_cfg.route_prio = 10;
  esp_netif_config_t ap_netif_cfg = {
      .base = &ap_base_cfg,
      .driver = NULL,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  st->netif = esp_netif_new(&ap_netif_cfg);
  if (st->netif) {
    esp_netif_set_hostname(st->netif, st->hostname);
    eos_net_set_ap_netif(st->netif);
  }
#endif

  EOS_LOGI("ap: access point network interface initialized (/dev/ap0)");
  return true;
}

void driver_net_ap_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  ap_state_t *st = (ap_state_t *)dev->state;

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  eos_net_ap_set_enabled(false);
  eos_net_wifi_update_mode();
  if (st->netif) {
    esp_netif_destroy_default_wifi(st->netif);
  }
#endif

  free(st);
  dev->state = NULL;
  eos_net_set_ap_netif(NULL);
  eos_net_set_ap_dev(NULL);
}

int driver_net_ap_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)dev;
  (void)path;
  (void)flags;
  (void)mode;
  return 0;
}

int driver_net_ap_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

int driver_net_ap_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) return -1;
  ap_state_t *st = (ap_state_t *)dev->state;

  switch (cmd) {
    case EOS_NETIF_IOCTL_GET_INFO: {
      eos_netif_info_t *info = va_arg(args, eos_netif_info_t *);
      if (!info) return -1;
      memset(info, 0, sizeof(*info));
      strncpy(info->name, "ap0", sizeof(info->name) - 1);
      info->type = EOS_NETIF_TYPE_AP;

      if (st->is_up) info->flags |= EOS_NETIF_FLAG_UP;
      if (st->dhcps_enabled) info->flags |= EOS_NETIF_FLAG_DHCP_SERVER;

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
        esp_netif_get_mac(st->netif, info->mac);

        // Read DNS
        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(st->netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
          if (dns.ip.type == ESP_IPADDR_TYPE_V4) {
            snprintf(info->dns_main, sizeof(info->dns_main), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
          }
        }
      }

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      wifi_sta_list_t sta_list = {0};
      if (st->is_up && esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        if (sta_list.num > 0) info->flags |= EOS_NETIF_FLAG_LINK_UP;
        snprintf(info->extra, sizeof(info->extra), "SSID \"%s\" (ch %d, %d clients)",
                 st->ap_cfg.ssid, st->ap_cfg.channel, sta_list.num);
      } else {
        snprintf(info->extra, sizeof(info->extra), "SSID \"%s\" (ch %d, idle)",
                 st->ap_cfg.ssid, st->ap_cfg.channel);
      }
#else
      snprintf(info->extra, sizeof(info->extra), "SSID \"%s\" (ch %d)",
               st->ap_cfg.ssid, st->ap_cfg.channel);
#endif
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_UP: {
      st->is_up = true;
      eos_net_ap_set_enabled(true);

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      // Apply AP config before bringing interface up
      wifi_config_t wcfg = {0};
      strncpy((char *)wcfg.ap.ssid, st->ap_cfg.ssid, sizeof(wcfg.ap.ssid) - 1);
      wcfg.ap.ssid_len = strlen(st->ap_cfg.ssid);
      strncpy((char *)wcfg.ap.password, st->ap_cfg.password, sizeof(wcfg.ap.password) - 1);
      wcfg.ap.channel = st->ap_cfg.channel;
      wcfg.ap.max_connection = st->ap_cfg.max_connection;
      wcfg.ap.ssid_hidden = st->ap_cfg.ssid_hidden;
      if (strlen(st->ap_cfg.password) == 0) {
        wcfg.ap.authmode = WIFI_AUTH_OPEN;
      } else {
        wcfg.ap.authmode = (wifi_auth_mode_t)st->ap_cfg.authmode;
      }

      esp_wifi_set_config(WIFI_IF_AP, &wcfg);
      eos_net_wifi_update_mode();

      if (st->netif && st->dhcps_enabled) {
        esp_netif_dhcps_start(st->netif);
      }
#endif
      EOS_LOGI("ap: interface set to UP (SSID: \"%s\")", st->ap_cfg.ssid);
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_DOWN: {
      st->is_up = false;
      eos_net_ap_set_enabled(false);
      eos_net_wifi_update_mode();
      if (st->netif) {
        esp_netif_dhcps_stop(st->netif);
      }
      EOS_LOGI("ap: interface set to DOWN");
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_IP: {
      const eos_netif_ip_info_t *ip_info = va_arg(args, const eos_netif_ip_info_t *);
      if (!ip_info || !st->netif) return -1;

      // Stop DHCP server before setting static IP
      esp_netif_dhcps_stop(st->netif);

      esp_netif_ip_info_t netif_ip = {0};
      netif_ip.ip.addr = esp_ip4addr_aton(ip_info->ip);
      netif_ip.netmask.addr = esp_ip4addr_aton(ip_info->netmask);
      netif_ip.gw.addr = esp_ip4addr_aton(ip_info->gw);

      esp_err_t err = esp_netif_set_ip_info(st->netif, &netif_ip);
      if (err != ESP_OK) {
        EOS_LOGE("ap: set_ip_info failed: %s", esp_err_to_name(err));
        return -1;
      }

      if (st->dhcps_enabled) {
        esp_netif_dhcps_start(st->netif);
      }
      EOS_LOGI("ap: configured IP: %s netmask %s gw %s",
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

    case EOS_NETIF_IOCTL_START_DHCPS: {
      if (!st->netif) return -1;
      st->dhcps_enabled = true;
      esp_netif_dhcps_start(st->netif);
      EOS_LOGI("ap: DHCP server enabled");
      return 0;
    }

    case EOS_NETIF_IOCTL_STOP_DHCPS: {
      if (!st->netif) return -1;
      st->dhcps_enabled = false;
      esp_netif_dhcps_stop(st->netif);
      EOS_LOGI("ap: DHCP server stopped");
      return 0;
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
      if (!mac || !st->netif) return -1;
      return (esp_netif_set_mac(st->netif, (uint8_t *)mac) == ESP_OK) ? 0 : -1;
    }

    case EOS_NETIF_IOCTL_GET_MAC: {
      uint8_t *out_mac = va_arg(args, uint8_t *);
      if (!out_mac || !st->netif) return -1;
      return (esp_netif_get_mac(st->netif, out_mac) == ESP_OK) ? 0 : -1;
    }

    case EOS_AP_IOCTL_SET_CONFIG: {
      const eos_ap_config_t *cfg = va_arg(args, const eos_ap_config_t *);
      if (!cfg) return -1;

      st->ap_cfg = *cfg;

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      wifi_config_t wcfg = {0};
      strncpy((char *)wcfg.ap.ssid, cfg->ssid, sizeof(wcfg.ap.ssid) - 1);
      wcfg.ap.ssid_len = strlen(cfg->ssid);
      strncpy((char *)wcfg.ap.password, cfg->password, sizeof(wcfg.ap.password) - 1);
      wcfg.ap.channel = cfg->channel ? cfg->channel : 1;
      wcfg.ap.max_connection = cfg->max_connection ? cfg->max_connection : 4;
      wcfg.ap.ssid_hidden = cfg->ssid_hidden;
      if (strlen(cfg->password) == 0) {
        wcfg.ap.authmode = WIFI_AUTH_OPEN;
      } else {
        wcfg.ap.authmode = (wifi_auth_mode_t)cfg->authmode;
      }

      esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wcfg);
      if (err != ESP_OK) {
        EOS_LOGE("ap: esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return -1;
      }
      EOS_LOGI("ap: configured SSID: \"%s\" (channel %d, max clients: %d)",
               cfg->ssid, wcfg.ap.channel, wcfg.ap.max_connection);
#endif
      return 0;
    }

    case EOS_AP_IOCTL_GET_CONFIG: {
      eos_ap_config_t *out_cfg = va_arg(args, eos_ap_config_t *);
      if (!out_cfg) return -1;
      *out_cfg = st->ap_cfg;
      return 0;
    }

    case EOS_AP_IOCTL_GET_STAS: {
      eos_ap_sta_list_t *list = va_arg(args, eos_ap_sta_list_t *);
      if (!list) return -1;
      memset(list, 0, sizeof(*list));

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      wifi_sta_list_t sta_list = {0};
      if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        size_t count = (sta_list.num < 10) ? sta_list.num : 10;
        list->count = count;
        for (size_t i = 0; i < count; i++) {
          memcpy(list->stas[i].mac, sta_list.sta[i].mac, 6);
          list->stas[i].rssi = sta_list.sta[i].rssi;
        }
      }
#endif
      return 0;
    }

    default:
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_net_ap = {
    EOS_DRV_INIT,
    .scope = "net",
    .name = "ap",
    .devname = "ap",
    .init = driver_net_ap_init,
    .shutdown = driver_net_ap_shutdown,
    .open = driver_net_ap_open,
    .close = driver_net_ap_close,
    .ioctl = driver_net_ap_ioctl,
};

#endif
