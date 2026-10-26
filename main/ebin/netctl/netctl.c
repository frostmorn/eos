#include "includes.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/netif.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char *s_auth_names[] = {
    "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA/WPA2", "WPA3_PSK", "WPA2/WPA3", "WAPI_PSK", "UNKNOWN"};

static const char *get_auth_name(uint8_t authmode) {
  if (authmode < sizeof(s_auth_names) / sizeof(s_auth_names[0]) - 1) {
    return s_auth_names[authmode];
  }
  return "UNKNOWN";
}

static void print_flags(uint32_t flags) {
  printf("<");
  bool first = true;
  if (flags & EOS_NETIF_FLAG_UP) {
    printf("%sUP", first ? "" : ",");
    first = false;
  }
  if (flags & EOS_NETIF_FLAG_LINK_UP) {
    printf("%sRUNNING", first ? "" : ",");
    first = false;
  }
  if (flags & EOS_NETIF_FLAG_DHCP_CLIENT) {
    printf("%sDHCPC", first ? "" : ",");
    first = false;
  }
  if (flags & EOS_NETIF_FLAG_DHCP_SERVER) {
    printf("%sDHCPS", first ? "" : ",");
    first = false;
  }
  if (flags & EOS_NETIF_FLAG_BRIDGE) {
    printf("%sBRIDGE", first ? "" : ",");
    first = false;
  }
  if (flags & EOS_NETIF_FLAG_PROMISC) {
    printf("%sPROMISC", first ? "" : ",");
    first = false;
  }
  if (first) {
    printf("DOWN");
  }
  printf(">");
}

static void show_iface_info(const char *name) {
  char path[32];
  snprintf(path, sizeof(path), "/dev/%s", name);

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    snprintf(path, sizeof(path), "/dev/%s0", name);
    fd = open(path, O_RDWR);
    if (fd < 0) {
      return;
    }
  }

  eos_netif_info_t info;
  memset(&info, 0, sizeof(info));
  if (ioctl(fd, EOS_NETIF_IOCTL_GET_INFO, &info) == 0) {
    printf("%-7s: flags=", info.name[0] ? info.name : name);
    print_flags(info.flags);
    printf("\n");

    if (info.type != EOS_NETIF_TYPE_RADIO) {
      if (info.ip.ip[0] && strcmp(info.ip.ip, "0.0.0.0") != 0) {
        printf("         inet %s  netmask %s  gateway %s\n",
               info.ip.ip, info.ip.netmask[0] ? info.ip.netmask : "255.255.255.0",
               info.ip.gw[0] ? info.ip.gw : "0.0.0.0");
      }
      if (info.mac[0] || info.mac[1] || info.mac[2] || info.mac[3] || info.mac[4] || info.mac[5]) {
        printf("         ether %02x:%02x:%02x:%02x:%02x:%02x\n",
               info.mac[0], info.mac[1], info.mac[2],
               info.mac[3], info.mac[4], info.mac[5]);
      } else {
        printf("         ether (unavailable)\n");
      }
      if (info.dns_main[0]) {
        printf("         dns %s%s%s\n", info.dns_main,
               info.dns_backup[0] ? ", " : "",
               info.dns_backup[0] ? info.dns_backup : "");
      }
    }

    if (info.extra[0]) {
      printf("         status: %s\n", info.extra);
    }
    printf("\n");
  }

  close(fd);
}

static int open_iface(const char *name) {
  char path[32];
  snprintf(path, sizeof(path), "/dev/%s", name);
  int fd = open(path, O_RDWR);
  if (fd < 0) {
    snprintf(path, sizeof(path), "/dev/%s0", name);
    fd = open(path, O_RDWR);
  }
  return fd;
}

static void print_usage(void) {
  printf("Usage: netctl [interface] [command] [args...]\n\n"
         "Commands:\n"
         "  netctl                         List all active network interfaces and status\n"
         "  netctl <if>                    Show configuration for <if>\n"
         "  netctl <if> status             Machine-readable status line for GUI/scripts\n"
         "  netctl <if> up                 Bring interface up\n"
         "  netctl <if> down               Bring interface down\n"
         "  netctl <if> ip <ip> [mask] [gw] Set static IPv4 configuration\n"
         "  netctl <if> dhcp [start|stop]  Start or stop DHCP client/server\n"
         "  netctl <if> connect <ssid> [pass] Connect station (sta0) to WiFi AP\n"
         "  netctl <if> disconnect         Disconnect station\n"
         "  netctl <if> ap <ssid> [pass] [ch] Configure SoftAP (ap0)\n"
         "  netctl <if> clients            List connected clients on SoftAP\n"
         "  netctl <if> scan [-p]          Scan for WiFi networks\n"
         "  netctl <br> add <port>         Attach port to network bridge (e.g. netctl br0 add sta0)\n"
         "  netctl <br> del <port>         Detach port from network bridge\n"
         "  netctl <br> napt on|off        Enable or disable NAPT routing on bridge\n"
         "  netctl <br> bridge <target|off> Bridging convenience alias\n");
}

int netctl_main(int argc, char **argv) {
  if (argc <= 1) {
    const char *candidates[] = {"sta0", "ap0", "wifi0", "br0", "eth0"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
      show_iface_info(candidates[i]);
    }
    return 0;
  }

  const char *ifname = argv[1];

  if (strcmp(ifname, "help") == 0 || strcmp(ifname, "-h") == 0 || strcmp(ifname, "--help") == 0) {
    print_usage();
    return 0;
  }

  if (argc == 2) {
    show_iface_info(ifname);
    return 0;
  }

  const char *cmd = argv[2];

  int fd = open_iface(ifname);
  if (fd < 0) {
    fprintf(stderr, "netctl: cannot open interface /dev/%s: %s\n", ifname, strerror(errno));
    return 1;
  }

  int ret = 0;

  if (strcmp(cmd, "status") == 0) {
    eos_netif_info_t info;
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, EOS_NETIF_IOCTL_GET_INFO, &info) == 0) {
      char mac_str[24] = "00:00:00:00:00:00";
      if (info.mac[0] || info.mac[1] || info.mac[2] || info.mac[3] || info.mac[4] || info.mac[5]) {
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 info.mac[0], info.mac[1], info.mac[2],
                 info.mac[3], info.mac[4], info.mac[5]);
      }
      printf("STATUS:if=%s,up=%d,link=%d,ip=%s,mask=%s,gw=%s,mac=%s,extra=%s\n",
             info.name[0] ? info.name : ifname,
             (info.flags & EOS_NETIF_FLAG_UP) ? 1 : 0,
             (info.flags & EOS_NETIF_FLAG_LINK_UP) ? 1 : 0,
             info.ip.ip[0] ? info.ip.ip : "0.0.0.0",
             info.ip.netmask[0] ? info.ip.netmask : "0.0.0.0",
             info.ip.gw[0] ? info.ip.gw : "0.0.0.0",
             mac_str,
             info.extra[0] ? info.extra : "none");
    } else {
      perror("netctl: failed to get interface status");
      ret = 1;
    }
  } else if (strcmp(cmd, "up") == 0) {
    if (ioctl(fd, EOS_NETIF_IOCTL_SET_UP) == 0) {
      printf("%s: interface brought UP\n", ifname);
    } else {
      perror("netctl: set UP failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "down") == 0) {
    if (ioctl(fd, EOS_NETIF_IOCTL_SET_DOWN) == 0) {
      printf("%s: interface brought DOWN\n", ifname);
    } else {
      perror("netctl: set DOWN failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "ip") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s ip <ip_address> [netmask] [gateway]\n", ifname);
      ret = 1;
    } else {
      eos_netif_ip_info_t ip;
      memset(&ip, 0, sizeof(ip));
      strncpy(ip.ip, argv[3], sizeof(ip.ip) - 1);
      if (argc > 4) {
        strncpy(ip.netmask, argv[4], sizeof(ip.netmask) - 1);
      } else {
        strncpy(ip.netmask, "255.255.255.0", sizeof(ip.netmask) - 1);
      }
      if (argc > 5) {
        strncpy(ip.gw, argv[5], sizeof(ip.gw) - 1);
      }
      if (ioctl(fd, EOS_NETIF_IOCTL_SET_IP, &ip) == 0) {
        printf("%s: configured static IP %s netmask %s\n", ifname, ip.ip, ip.netmask);
      } else {
        perror("netctl: set IP failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "dhcp") == 0) {
    if (argc >= 4 && strcmp(argv[3], "stop") == 0) {
      if (ioctl(fd, EOS_NETIF_IOCTL_STOP_DHCPC) == 0 ||
          ioctl(fd, EOS_NETIF_IOCTL_STOP_DHCPS) == 0) {
        printf("%s: DHCP stopped\n", ifname);
      } else {
        perror("netctl: stop DHCP failed");
        ret = 1;
      }
    } else {
      if (ioctl(fd, EOS_NETIF_IOCTL_START_DHCPC) == 0 ||
          ioctl(fd, EOS_NETIF_IOCTL_START_DHCPS) == 0) {
        printf("%s: DHCP started\n", ifname);
      } else {
        perror("netctl: start DHCP failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "connect") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s connect <ssid> [password]\n", ifname);
      ret = 1;
    } else {
      eos_sta_connect_t cfg;
      memset(&cfg, 0, sizeof(cfg));
      strncpy(cfg.ssid, argv[3], sizeof(cfg.ssid) - 1);
      if (argc > 4) {
        strncpy(cfg.password, argv[4], sizeof(cfg.password) - 1);
      }
      cfg.timeout_ms = 10000;

      printf("%s: connecting to \"%s\"...\n", ifname, cfg.ssid);
      if (ioctl(fd, EOS_STA_IOCTL_CONNECT, &cfg) == 0) {
        printf("%s: connected to \"%s\"\n", ifname, cfg.ssid);
      } else {
        perror("netctl: connect failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "disconnect") == 0) {
    if (ioctl(fd, EOS_STA_IOCTL_DISCONNECT) == 0) {
      printf("%s: disconnected\n", ifname);
    } else {
      perror("netctl: disconnect failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "ap") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s ap <ssid> [password] [channel]\n", ifname);
      ret = 1;
    } else {
      eos_ap_config_t ap_cfg;
      memset(&ap_cfg, 0, sizeof(ap_cfg));
      strncpy(ap_cfg.ssid, argv[3], sizeof(ap_cfg.ssid) - 1);
      if (argc > 4 && strcmp(argv[4], "-") != 0 && strlen(argv[4]) > 0) {
        strncpy(ap_cfg.password, argv[4], sizeof(ap_cfg.password) - 1);
        ap_cfg.authmode = 3; // WPA2_PSK
      } else {
        ap_cfg.authmode = 0; // OPEN
      }
      ap_cfg.channel = (argc > 5) ? (uint8_t)atoi(argv[5]) : 1;
      ap_cfg.max_connection = 4;

      if (ioctl(fd, EOS_AP_IOCTL_SET_CONFIG, &ap_cfg) == 0 &&
          ioctl(fd, EOS_NETIF_IOCTL_SET_UP) == 0) {
        printf("%s: SoftAP started (SSID: \"%s\", ch %d, auth: %s)\n",
               ifname, ap_cfg.ssid, ap_cfg.channel, get_auth_name(ap_cfg.authmode));
      } else {
        perror("netctl: configure AP failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "clients") == 0) {
    eos_ap_sta_list_t list;
    memset(&list, 0, sizeof(list));
    if (ioctl(fd, EOS_AP_IOCTL_GET_STAS, &list) == 0) {
      printf("%s: %u connected station(s):\n", ifname, (unsigned)list.count);
      for (size_t i = 0; i < list.count; i++) {
        printf("  [%zu] %02x:%02x:%02x:%02x:%02x:%02x  RSSI: %d dBm\n",
               i + 1,
               list.stas[i].mac[0], list.stas[i].mac[1], list.stas[i].mac[2],
               list.stas[i].mac[3], list.stas[i].mac[4], list.stas[i].mac[5],
               list.stas[i].rssi);
      }
    } else {
      perror("netctl: get clients failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "add") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s add <port_interface>\n", ifname);
      ret = 1;
    } else {
      const char *port = argv[3];
      if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_ADD_IF, port) == 0) {
        printf("%s: attached port \"%s\"\n", ifname, port);
      } else {
        perror("netctl: bridge add failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "del") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s del <port_interface>\n", ifname);
      ret = 1;
    } else {
      const char *port = argv[3];
      if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_DEL_IF, port) == 0) {
        printf("%s: removed port \"%s\"\n", ifname, port);
      } else {
        perror("netctl: bridge del failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "napt") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s napt on|off\n", ifname);
      ret = 1;
    } else {
      int enable = (strcmp(argv[3], "on") == 0) ? 1 : 0;
      if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_SET_NAPT, enable) == 0) {
        printf("%s: NAPT %s\n", ifname, enable ? "enabled" : "disabled");
      } else {
        perror("netctl: set NAPT failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "bridge") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s bridge <target-if|off>\n", ifname);
      ret = 1;
    } else if (strcmp(argv[3], "off") == 0) {
      if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_SET_NAPT, 0) == 0) {
        printf("%s: bridge/NAPT disabled\n", ifname);
      } else {
        perror("netctl: disable bridge failed");
        ret = 1;
      }
    } else {
      const char *target = argv[3];
      if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_ADD_IF, target) == 0) {
        printf("%s: bridged with \"%s\"\n", ifname, target);
      } else {
        perror("netctl: bridge add failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "scan") == 0) {
    bool passive = false;
    uint8_t channel = 0;
    for (int i = 3; i < argc; i++) {
      if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--passive") == 0) {
        passive = true;
      } else if (atoi(argv[i]) > 0) {
        channel = (uint8_t)atoi(argv[i]);
      }
    }

    eos_wifi_scan_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.mode = passive ? EOS_WIFI_SCAN_PASSIVE : EOS_WIFI_SCAN_ACTIVE;
    scfg.channel = channel;
    scfg.show_hidden = true;
    scfg.dwell_time_ms = passive ? 200 : 120;

    printf("Scanning for WiFi networks (%s mode%s%d)...\n",
           passive ? "passive" : "active",
           channel ? ", channel " : "", channel);

    if (ioctl(fd, EOS_RADIO_IOCTL_SCAN_START, &scfg) != 0) {
      perror("netctl: scan start failed");
      close(fd);
      return 1;
    }

    uint16_t ap_count = 0;
    ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_COUNT, &ap_count);

    if (ap_count == 0) {
      printf("No access points found.\n");
      printf("SCAN_DONE:count=0\n");
    } else {
      eos_wifi_scan_ap_t *aps = calloc(ap_count, sizeof(eos_wifi_scan_ap_t));
      if (aps) {
        eos_wifi_scan_results_t res = {
            .aps = aps,
            .max_count = ap_count,
            .count = 0,
        };
        ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_RESULTS, &res);

        printf("\nFound %zu access point(s):\n", res.count);
        printf("%-32s  %-17s  %4s  %2s  %-12s\n",
               "SSID", "BSSID", "RSSI", "CH", "AUTH");
        printf("-----------------------------------------------------------------------\n");
        for (size_t i = 0; i < res.count; i++) {
          const char *ssid = res.aps[i].is_hidden ? "<hidden>" : res.aps[i].ssid;
          printf("%-32s  %02x:%02x:%02x:%02x:%02x:%02x  %4d  %2u  %-12s\n",
                 ssid,
                 res.aps[i].bssid[0], res.aps[i].bssid[1], res.aps[i].bssid[2],
                 res.aps[i].bssid[3], res.aps[i].bssid[4], res.aps[i].bssid[5],
                 res.aps[i].rssi, (unsigned)res.aps[i].channel,
                 get_auth_name(res.aps[i].authmode));
        }
        printf("\n");
        printf("SCAN_DONE:count=%zu\n", res.count);
        free(aps);
      }
    }
  } else {
    fprintf(stderr, "netctl: unknown command \"%s\"\n", cmd);
    print_usage();
    ret = 1;
  }

  close(fd);
  return ret;
}

EOS_BIN_ATTR eos_bin_t netctl_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "netctl",
    .name = "Network Interface Control",
    .group = "Network",
    .description = "Manage network interfaces (STA, AP, bridge, IP, DHCP)",
    .entry_point = netctl_main};
