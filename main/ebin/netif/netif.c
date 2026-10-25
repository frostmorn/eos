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
    // If failed, try appending "0" (e.g. "sta" -> "sta0")
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
  printf("Usage: netif [interface] [command] [args...]\n\n"
         "Commands:\n"
         "  netif                         List all network interfaces and status\n"
         "  netif <if>                    Show configuration for <if>\n"
         "  netif <if> up                 Bring interface up\n"
         "  netif <if> down               Bring interface down\n"
         "  netif <if> ip <ip> [mask] [gw] Set static IPv4 configuration\n"
         "  netif <if> dhcp [start|stop]  Start or stop DHCP client/server\n"
         "  netif <if> connect <ssid> [pass] Connect station to WiFi AP\n"
         "  netif <if> disconnect         Disconnect station\n"
         "  netif <if> ap <ssid> [pass] [ch] Configure SoftAP\n"
         "  netif <if> clients            List connected clients on SoftAP\n"
         "  netif <if> radio on|off       Turn physical radio on or off\n"
         "  netif <if> channel <1-13>     Set physical channel\n"
         "  netif <if> scan [-p|--passive] Scan for WiFi networks\n"
         "  netif <if> bridge <target-if> Attach port to bridge / enable NAPT\n"
         "  netif <if> promisc on|off     Enable/disable 802.11 promiscuous mode\n"
         "  netif <if> dump [count]       Capture and display raw 802.11 frames\n"
         "  netif <if> send <hex-bytes>   Inject a raw 802.11 packet\n");
}

int netif_main(int argc, char **argv) {
  if (argc <= 1) {
    // List all known interface candidates
    const char *candidates[] = {"sta0", "ap0", "wifi0", "br0"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
      show_iface_info(candidates[i]);
    }
    return 0;
  }

  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    print_usage();
    return 0;
  }

  const char *ifname = argv[1];

  if (argc == 2) {
    show_iface_info(ifname);
    return 0;
  }

  const char *cmd = argv[2];
  int fd = open_iface(ifname);
  if (fd < 0) {
    fprintf(stderr, "netif: interface \"%s\" not found in /dev/\n", ifname);
    return 1;
  }

  int ret = 0;

  if (strcmp(cmd, "up") == 0) {
    if (ioctl(fd, EOS_NETIF_IOCTL_SET_UP) == 0) {
      printf("%s: interface brought UP\n", ifname);
    } else {
      perror("netif: set up failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "down") == 0) {
    if (ioctl(fd, EOS_NETIF_IOCTL_SET_DOWN) == 0) {
      printf("%s: interface brought DOWN\n", ifname);
    } else {
      perror("netif: set down failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "ip") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s ip <address> [netmask] [gateway]\n", ifname);
      ret = 1;
    } else {
      eos_netif_ip_info_t ip_info;
      memset(&ip_info, 0, sizeof(ip_info));
      strncpy(ip_info.ip, argv[3], sizeof(ip_info.ip) - 1);
      strncpy(ip_info.netmask, (argc > 4) ? argv[4] : "255.255.255.0", sizeof(ip_info.netmask) - 1);
      strncpy(ip_info.gw, (argc > 5) ? argv[5] : argv[3], sizeof(ip_info.gw) - 1);

      if (ioctl(fd, EOS_NETIF_IOCTL_SET_IP, &ip_info) == 0) {
        printf("%s: IP set to %s netmask %s gateway %s\n",
               ifname, ip_info.ip, ip_info.netmask, ip_info.gw);
      } else {
        perror("netif: set IP failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "dhcp") == 0) {
    bool stop = (argc > 3 && strcmp(argv[3], "stop") == 0);
    int ioctl_cmd = stop ? EOS_NETIF_IOCTL_STOP_DHCPC : EOS_NETIF_IOCTL_START_DHCPC;
    if (ioctl(fd, ioctl_cmd) == 0) {
      printf("%s: DHCP client %s\n", ifname, stop ? "stopped" : "started");
    } else {
      perror("netif: DHCP client control failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "connect") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s connect <ssid> [password]\n", ifname);
      ret = 1;
    } else {
      eos_sta_connect_t conn;
      memset(&conn, 0, sizeof(conn));
      strncpy(conn.ssid, argv[3], sizeof(conn.ssid) - 1);
      if (argc > 4) {
        strncpy(conn.password, argv[4], sizeof(conn.password) - 1);
      }
      conn.timeout_ms = 12000;

      printf("%s: connecting to \"%s\"...\n", ifname, conn.ssid);
      if (ioctl(fd, EOS_STA_IOCTL_CONNECT, &conn) == 0) {
        printf("%s: successfully connected to \"%s\"!\n", ifname, conn.ssid);
      } else {
        fprintf(stderr, "%s: connection to \"%s\" failed or timed out\n", ifname, conn.ssid);
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "disconnect") == 0) {
    if (ioctl(fd, EOS_STA_IOCTL_DISCONNECT) == 0) {
      printf("%s: disconnected\n", ifname);
    } else {
      perror("netif: disconnect failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "ap") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s ap <ssid> [password] [channel]\n", ifname);
      ret = 1;
    } else {
      eos_ap_config_t ap_cfg;
      memset(&ap_cfg, 0, sizeof(ap_cfg));
      strncpy(ap_cfg.ssid, argv[3], sizeof(ap_cfg.ssid) - 1);
      if (argc > 4 && strcmp(argv[4], "-") != 0) {
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
        perror("netif: configure AP failed");
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
      perror("netif: get clients failed");
      ret = 1;
    }
  } else if (strcmp(cmd, "radio") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s radio on|off\n", ifname);
      ret = 1;
    } else {
      int power = (strcmp(argv[3], "on") == 0) ? 1 : 0;
      if (ioctl(fd, EOS_RADIO_IOCTL_POWER, power) == 0) {
        printf("%s: radio turned %s\n", ifname, power ? "ON" : "OFF");
      } else {
        perror("netif: radio power failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "channel") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s channel <1-13>\n", ifname);
      ret = 1;
    } else {
      uint8_t ch = (uint8_t)atoi(argv[3]);
      if (ioctl(fd, EOS_RADIO_IOCTL_SET_CHANNEL, ch) == 0) {
        printf("%s: channel set to %d\n", ifname, ch);
      } else {
        perror("netif: set channel failed");
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
      perror("netif: scan start failed");
      close(fd);
      return 1;
    }

    uint16_t ap_count = 0;
    ioctl(fd, EOS_RADIO_IOCTL_SCAN_GET_COUNT, &ap_count);

    if (ap_count == 0) {
      printf("No access points found.\n");
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
        free(aps);
      }
    }
  } else if (strcmp(cmd, "bridge") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s bridge <target-if|off>\n", ifname);
      ret = 1;
    } else if (strcmp(argv[3], "off") == 0) {
      if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_SET_NAPT, 0) == 0) {
        printf("%s: bridge/NAPT disabled\n", ifname);
      } else {
        perror("netif: disable bridge failed");
        ret = 1;
      }
    } else {
      const char *target = argv[3];
      if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_ADD_IF, target) == 0) {
        printf("%s: bridged with \"%s\" (NAPT routing enabled)\n", ifname, target);
      } else {
        perror("netif: bridge add failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "promisc") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s promisc on|off\n", ifname);
      ret = 1;
    } else {
      bool enable = (strcmp(argv[3], "on") == 0);
      eos_wifi_promisc_cfg_t pcfg = {
          .enable = enable,
          .filter_mgmt = true,
          .filter_ctrl = true,
          .filter_data = true,
          .filter_misc = true,
      };
      if (ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg) == 0) {
        printf("%s: promiscuous mode %s\n", ifname, enable ? "enabled" : "disabled");
      } else {
        perror("netif: set promiscuous failed");
        ret = 1;
      }
    }
  } else if (strcmp(cmd, "dump") == 0) {
    // Capture and display raw 802.11 packets
    eos_wifi_promisc_cfg_t pcfg = {
        .enable = true,
        .filter_mgmt = true,
        .filter_ctrl = true,
        .filter_data = true,
    };
    ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg);

    int count = (argc > 3) ? atoi(argv[3]) : 10;
    if (count <= 0) count = 10;

    printf("Capturing %d raw 802.11 frames from %s...\n\n", count, ifname);
    for (int i = 0; i < count; i++) {
      eos_wifi_raw_packet_t pkt;
      ssize_t n = read(fd, &pkt, sizeof(pkt));
      if (n > 0) {
        uint16_t fc = pkt.payload[0] | (pkt.payload[1] << 8);
        uint8_t type = (fc >> 2) & 0x3;
        uint8_t subtype = (fc >> 4) & 0xF;

        const char *tname = (type == 0) ? "MGMT" : (type == 1) ? "CTRL" : (type == 2) ? "DATA" : "RESV";

        printf("[%2d] CH %2u | RSSI %4d dBm | Len %4u | %s (sub %u)\n",
               i + 1, pkt.channel, pkt.rssi, pkt.length, tname, subtype);
        if (pkt.length >= 10) {
          printf("     Addr1: %02x:%02x:%02x:%02x:%02x:%02x  Addr2: %02x:%02x:%02x:%02x:%02x:%02x\n",
                 pkt.payload[4], pkt.payload[5], pkt.payload[6],
                 pkt.payload[7], pkt.payload[8], pkt.payload[9],
                 pkt.payload[10], pkt.payload[11], pkt.payload[12],
                 pkt.payload[13], pkt.payload[14], pkt.payload[15]);
        }
      } else {
        printf("[%2d] Timeout waiting for 802.11 packet\n", i + 1);
      }
    }
  } else if (strcmp(cmd, "send") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netif %s send <hex-bytes>\n", ifname);
      ret = 1;
    } else {
      const char *hex = argv[3];
      size_t hex_len = strlen(hex);
      if (hex_len % 2 != 0 || hex_len / 2 > 512) {
        fprintf(stderr, "netif: invalid hex string length\n");
        ret = 1;
      } else {
        size_t byte_len = hex_len / 2;
        uint8_t *buf = malloc(byte_len);
        if (!buf) {
          ret = 1;
        } else {
          for (size_t i = 0; i < byte_len; i++) {
            char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
            buf[i] = (uint8_t)strtoul(byte_str, NULL, 16);
          }
          ssize_t sent = write(fd, buf, byte_len);
          if (sent > 0) {
            printf("%s: transmitted %zd raw 802.11 bytes\n", ifname, sent);
          } else {
            perror("netif: raw 802.11 transmit failed");
            ret = 1;
          }
          free(buf);
        }
      }
    }
  } else {
    fprintf(stderr, "netif: unknown command \"%s\"\n", cmd);
    print_usage();
    ret = 1;
  }

  close(fd);
  return ret;
}

EOS_BIN_ATTR eos_bin_t netif_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "netif",
    .name = "Network Interface Config",
    .group = "Network",
    .description = "Configure network interfaces, WiFi radio, scan, AP/STA, and bridges",
    .entry_point = netif_main};
