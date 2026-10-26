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

#define DEFAULT_WLAN_DEV "/dev/wifi0"

static const char *s_auth_names[] = {
    "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA/WPA2", "WPA3_PSK", "WPA2/WPA3", "WAPI_PSK", "UNKNOWN"};

static const char *get_auth_name(uint8_t authmode) {
  if (authmode < sizeof(s_auth_names) / sizeof(s_auth_names[0]) - 1) {
    return s_auth_names[authmode];
  }
  return "UNKNOWN";
}

static int open_wlan(const char *name) {
  char path[32];
  if (!name || !name[0]) {
    name = "wifi0";
  }
  if (name[0] == '/') {
    strncpy(path, name, sizeof(path) - 1);
  } else {
    snprintf(path, sizeof(path), "/dev/%s", name);
  }
  path[sizeof(path) - 1] = '\0';

  int fd = open(path, O_RDWR);
  if (fd < 0 && strstr(path, "wifi0") == NULL && strstr(path, "wifi") != NULL) {
    snprintf(path, sizeof(path), "/dev/wifi0");
    fd = open(path, O_RDWR);
  }
  if (fd < 0 && strcmp(path, "/dev/wifi0") == 0) {
    snprintf(path, sizeof(path), "/dev/wifi");
    fd = open(path, O_RDWR);
  }
  return fd;
}

static void print_usage(void) {
  printf("Usage: wlanctl <command> [args...] [interface]\n\n"
         "Commands:\n"
         "  radio on|off              Turn physical WLAN radio power on or off\n"
         "  getmac [dev]              Display hardware/assigned MAC address\n"
         "  setmac <mac> [dev]        Set MAC address (format: XX:XX:XX:XX:XX:XX)\n"
         "  channel <1-13> [dev]      Set physical RF channel\n"
         "  sniffer on|off [dev]      Enable or disable 802.11 sniffer (promiscuous) mode\n"
         "  dump [count] [dev]        Capture and display raw 802.11 frames\n"
         "  send <hex-bytes> [dev]    Inject raw 802.11 packet frame\n"
         "  scan [-p|--passive] [ch]  Scan for 802.11 wireless networks\n\n"
         "Default interface: wifi0\n");
}

int wlanctl_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 0;
  }

  const char *cmd = argv[1];

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
    print_usage();
    return 0;
  }

  if (strcmp(cmd, "radio") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: wlanctl radio on|off [dev]\n");
      return 1;
    }
    const char *dev = (argc > 3) ? argv[3] : "wifi0";
    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open radio device");
      return 1;
    }

    int power = (strcmp(argv[2], "on") == 0) ? 1 : 0;
    if (ioctl(fd, EOS_RADIO_IOCTL_POWER, power) == 0) {
      printf("%s: radio turned %s\n", dev, power ? "ON" : "OFF");
      close(fd);
      return 0;
    } else {
      perror("wlanctl: radio power failed");
      close(fd);
      return 1;
    }
  }

  if (strcmp(cmd, "getmac") == 0) {
    const char *dev = (argc > 2) ? argv[2] : "wifi0";
    int fd = open_wlan(dev);
    if (fd < 0 && strcmp(dev, "wifi0") == 0) {
      dev = "sta0";
      fd = open_wlan(dev);
    }
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      return 1;
    }

    uint8_t mac[6] = {0};
    if (ioctl(fd, EOS_NETIF_IOCTL_GET_MAC, mac) == 0) {
      printf("%s: MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
             dev, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      close(fd);
      return 0;
    }

    // Try fallback to sta0 if wifi0 failed
    close(fd);
    if (strcmp(dev, "wifi0") == 0) {
      fd = open_wlan("sta0");
      if (fd >= 0) {
        if (ioctl(fd, EOS_NETIF_IOCTL_GET_MAC, mac) == 0) {
          printf("sta0: MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
          close(fd);
          return 0;
        }
        close(fd);
      }
    }

    perror("wlanctl: get MAC failed");
    return 1;
  }

  if (strcmp(cmd, "setmac") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: wlanctl setmac <mac> [dev]\n");
      return 1;
    }
    const char *mac_str = argv[2];
    const char *dev = (argc > 3) ? argv[3] : "wifi0";
    unsigned int m[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6 &&
        sscanf(mac_str, "%x-%x-%x-%x-%x-%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) {
      fprintf(stderr, "wlanctl: invalid MAC format (expected XX:XX:XX:XX:XX:XX)\n");
      return 1;
    }

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)m[i];

    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      return 1;
    }

    if (ioctl(fd, EOS_NETIF_IOCTL_SET_MAC, mac) == 0) {
      printf("%s: MAC set to %02x:%02x:%02x:%02x:%02x:%02x\n",
             dev, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      close(fd);
      return 0;
    } else {
      perror("wlanctl: set MAC failed");
      close(fd);
      return 1;
    }
  }

  if (strcmp(cmd, "channel") == 0) {
    const char *dev = (argc > 3) ? argv[3] : "wifi0";
    if (argc < 3) {
      int fd = open_wlan(dev);
      if (fd < 0) {
        perror("wlanctl: failed to open radio device");
        return 1;
      }
      uint8_t ch = 0;
      if (ioctl(fd, EOS_RADIO_IOCTL_GET_CHANNEL, &ch) == 0) {
        printf("%s: channel %u\n", dev, ch);
        close(fd);
        return 0;
      } else {
        perror("wlanctl: get channel failed");
        close(fd);
        return 1;
      }
    }
    uint8_t ch = (uint8_t)atoi(argv[2]);
    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open radio device");
      return 1;
    }

    if (ioctl(fd, EOS_RADIO_IOCTL_SET_CHANNEL, ch) == 0) {
      printf("%s: channel set to %d\n", dev, ch);
      close(fd);
      return 0;
    } else {
      perror("wlanctl: set channel failed");
      close(fd);
      return 1;
    }
  }

  if (strcmp(cmd, "sniffer") == 0 || strcmp(cmd, "promisc") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: wlanctl sniffer on|off [dev]\n");
      return 1;
    }
    bool enable = (strcmp(argv[2], "on") == 0);
    const char *dev = (argc > 3) ? argv[3] : "wifi0";
    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      return 1;
    }

    eos_wifi_promisc_cfg_t pcfg = {
        .enable = enable,
        .filter_mgmt = true,
        .filter_ctrl = true,
        .filter_data = true,
        .filter_misc = true,
    };
    if (ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg) == 0) {
      printf("%s: 802.11 sniffer mode %s\n", dev, enable ? "enabled" : "disabled");
      close(fd);
      return 0;
    } else {
      perror("wlanctl: set sniffer mode failed");
      close(fd);
      return 1;
    }
  }

  if (strcmp(cmd, "dump") == 0) {
    int count = (argc > 2 && atoi(argv[2]) > 0) ? atoi(argv[2]) : 10;
    const char *dev = (argc > 3) ? argv[3] : "wifi0";

    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      return 1;
    }

    eos_wifi_promisc_cfg_t pcfg = {
        .enable = true,
        .filter_mgmt = true,
        .filter_ctrl = true,
        .filter_data = true,
        .filter_misc = true,
    };
    ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg);

    printf("Capturing %d raw 802.11 frames from %s...\n\n", count, dev);
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
        if (pkt.length >= 16) {
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
    close(fd);
    return 0;
  }

  if (strcmp(cmd, "send") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: wlanctl send <hex-bytes> [dev]\n");
      return 1;
    }
    const char *hex = argv[2];
    const char *dev = (argc > 3) ? argv[3] : "wifi0";
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > 512) {
      fprintf(stderr, "wlanctl: invalid hex string length\n");
      return 1;
    }

    size_t byte_len = hex_len / 2;
    uint8_t *buf = malloc(byte_len);
    if (!buf) {
      return 1;
    }
    for (size_t i = 0; i < byte_len; i++) {
      char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
      buf[i] = (uint8_t)strtoul(byte_str, NULL, 16);
    }

    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      free(buf);
      return 1;
    }

    ssize_t sent = write(fd, buf, byte_len);
    free(buf);
    if (sent > 0) {
      printf("%s: transmitted %zd raw 802.11 bytes\n", dev, sent);
      close(fd);
      return 0;
    } else {
      perror("wlanctl: raw 802.11 transmit failed");
      close(fd);
      return 1;
    }
  }

  if (strcmp(cmd, "scan") == 0) {
    bool passive = false;
    uint8_t channel = 0;
    const char *dev = "wifi0";

    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--passive") == 0) {
        passive = true;
      } else if (strncmp(argv[i], "wifi", 4) == 0) {
        dev = argv[i];
      } else if (atoi(argv[i]) > 0) {
        channel = (uint8_t)atoi(argv[i]);
      }
    }

    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open radio device");
      return 1;
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
      perror("wlanctl: scan start failed");
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
    close(fd);
    return 0;
  }

  fprintf(stderr, "wlanctl: unknown command \"%s\"\n", cmd);
  print_usage();
  return 1;
}

EOS_BIN_ATTR eos_bin_t wlanctl_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "wlanctl",
    .name = "WLAN Radio Control",
    .group = "Network",
    .description = "Control physical WLAN radio, MAC, channel, sniffer, and frame injection",
    .entry_point = wlanctl_main};
