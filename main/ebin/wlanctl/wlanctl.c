#include "includes.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/netif.h"
#include "emisc/netmisc.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DEFAULT_WLAN_DEV "/dev/wifi0"

static int open_wlan(const char *name) {
  if (!name || !name[0]) {
    name = "wifi0";
  }
  char path[32];
  if (name[0] == '/') {
    snprintf(path, sizeof(path), "%s", name);
  } else {
    snprintf(path, sizeof(path), "/dev/%s", name);
  }
  return open(path, O_RDWR);
}

static void print_usage(void) {
  printf("Usage: wlanctl <command> [args...] [interface]\n\n"
         "Commands:\n"
         "  radio on|off|status          Turn physical WLAN radio power on or off, or query status\n"
         "  getmac [dev]                 Display hardware/assigned MAC address\n"
         "  setmac <mac> [dev]           Set MAC address (format: XX:XX:XX:XX:XX:XX)\n"
         "  channel <1-13> [dev]         Set physical RF channel\n"
         "  sniffer on|off [dev]         Enable or disable 802.11 sniffer (promiscuous) mode\n"
         "  dump [count] [dev] [opts]    Capture 802.11 frames (0=infinite, opts: --type <t>, --bssid <m>,\n"
         "                               --src <m>, --dst <m>, --hop <ms>, --verbose, --pcap <file>)\n"
         "  send <hex-bytes> [dev]       Inject raw 802.11 packet frame\n"
         "  scan [-p|--passive] [ch]     Scan for 802.11 wireless networks\n\n"
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
      fprintf(stderr, "Usage: wlanctl radio on|off|status [dev]\n");
      return 1;
    }
    const char *dev = (argc > 3) ? argv[3] : "wifi0";
    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open radio device");
      return 1;
    }

    if (strcmp(argv[2], "status") == 0) {
      int power = 0;
      if (ioctl(fd, EOS_RADIO_IOCTL_GET_POWER, &power) == 0) {
        printf("%s: radio turned %s\n", dev, power ? "ON" : "OFF");
        close(fd);
        return 0;
      } else {
        perror("wlanctl: radio get power failed");
        close(fd);
        return 1;
      }
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
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      return 1;
    }

    uint8_t mac[6] = {0};
    if (ioctl(fd, EOS_NETIF_IOCTL_GET_MAC, mac) == 0) {
      char mac_str[18];
      eos_net_format_mac(mac, mac_str, sizeof(mac_str));
      printf("%s: MAC address %s\n", dev, mac_str);
      close(fd);
      return 0;
    }

    perror("wlanctl: get MAC failed");
    close(fd);
    return 1;
  }

  if (strcmp(cmd, "setmac") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: wlanctl setmac <mac> [dev]\n");
      return 1;
    }
    const char *mac_str = argv[2];
    const char *dev = (argc > 3) ? argv[3] : "wifi0";
    uint8_t mac[6];
    if (!eos_net_parse_mac(mac_str, mac)) {
      fprintf(stderr, "wlanctl: invalid MAC format (expected XX:XX:XX:XX:XX:XX)\n");
      return 1;
    }

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

    eos_wifi_promisc_cfg_t pcfg = {.enable = enable};
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
    int count = 10;
    const char *dev = "wifi0";
    const char *type_filter = NULL;
    uint8_t bssid_filter[6];
    bool has_bssid = false;
    uint8_t src_filter[6];
    bool has_src = false;
    uint8_t dst_filter[6];
    bool has_dst = false;
    uint32_t hop_interval_ms = 0;
    bool verbose = false;
    const char *pcap_path = NULL;

    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
        type_filter = argv[++i];
      } else if (strcmp(argv[i], "--bssid") == 0 && i + 1 < argc) {
        has_bssid = eos_net_parse_mac(argv[++i], bssid_filter);
      } else if (strcmp(argv[i], "--src") == 0 && i + 1 < argc) {
        has_src = eos_net_parse_mac(argv[++i], src_filter);
      } else if (strcmp(argv[i], "--dst") == 0 && i + 1 < argc) {
        has_dst = eos_net_parse_mac(argv[++i], dst_filter);
      } else if (strcmp(argv[i], "--hop") == 0 && i + 1 < argc) {
        hop_interval_ms = (uint32_t)atoi(argv[++i]);
      } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
        verbose = true;
      } else if (strcmp(argv[i], "--pcap") == 0 && i + 1 < argc) {
        pcap_path = argv[++i];
      } else if (argv[i][0] != '-') {
        if (atoi(argv[i]) > 0 || strcmp(argv[i], "0") == 0) {
          count = atoi(argv[i]);
        } else if (strncmp(argv[i], "wifi", 4) == 0 || argv[i][0] == '/') {
          dev = argv[i];
        }
      }
    }

    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      return 1;
    }

    eos_wifi_promisc_cfg_t pcfg = {.enable = true};
    ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg);

    FILE *pcap_file = NULL;
    if (pcap_path) {
      pcap_file = fopen(pcap_path, "wb");
      if (pcap_file) {
        uint8_t hdr_buf[32];
        size_t hdr_len = eos_net_pcap_write_global_header(hdr_buf, sizeof(hdr_buf));
        fwrite(hdr_buf, 1, hdr_len, pcap_file);
        printf("Saving captured frames to PCAP file: %s\n", pcap_path);
      } else {
        perror("wlanctl: failed to open PCAP file for writing");
      }
    }

    bool infinite = (count <= 0);
    printf("Capturing %s 802.11 frames from %s%s%s%s...\n\n",
           infinite ? "infinite" : "", dev,
           type_filter ? " [type=" : "", type_filter ? type_filter : "", type_filter ? "]" : "");

    int captured = 0;
    int displayed = 0;
    uint8_t current_channel = 1;
    int timeouts = 0;

    while (infinite || displayed < count) {
      if (hop_interval_ms > 0) {
        current_channel++;
        if (current_channel > 13) current_channel = 1;
        ioctl(fd, EOS_RADIO_IOCTL_SET_CHANNEL, (int)current_channel);
        usleep(hop_interval_ms * 1000);
      }

      eos_wifi_raw_packet_t pkt;
      ssize_t n = read(fd, &pkt, sizeof(pkt));
      if (n > 0) {
        captured++;
        if (!eos_net_wifi_match_type(pkt.type_name, type_filter)) continue;
        if (has_bssid && (!pkt.has_addrs || !eos_net_wifi_match_mac(pkt.addr3, bssid_filter))) continue;
        if (has_src && (!pkt.has_addrs || !eos_net_wifi_match_mac(pkt.addr2, src_filter))) continue;
        if (has_dst && (!pkt.has_addrs || !eos_net_wifi_match_mac(pkt.addr1, dst_filter))) continue;

        displayed++;
        printf("[%3d] CH %2u | RSSI %4d dBm | Len %4u | %s (sub %u)\n",
               displayed, pkt.channel, pkt.rssi, pkt.length,
               pkt.type_name[0] ? pkt.type_name : "UNKNOWN",
               pkt.frame_subtype);
        if (pkt.has_addrs) {
          char a1[18], a2[18], a3[18];
          eos_net_format_mac(pkt.addr1, a1, sizeof(a1));
          eos_net_format_mac(pkt.addr2, a2, sizeof(a2));
          eos_net_format_mac(pkt.addr3, a3, sizeof(a3));
          printf("      DA (Addr1): %s  SA (Addr2): %s  BSSID (Addr3): %s\n", a1, a2, a3);
        }
        if (verbose && pkt.length > 0) {
          printf("      Payload hex (%u bytes):\n      ", pkt.length);
          for (uint16_t j = 0; j < pkt.length && j < 64; j++) {
            printf("%02x ", pkt.payload[j]);
            if ((j + 1) % 16 == 0) printf("\n      ");
          }
          printf("\n");
        }

        if (pcap_file) {
          uint8_t phdr_buf[16];
          size_t phdr_len = eos_net_pcap_write_packet_header(phdr_buf, sizeof(phdr_buf),
                                                             0, 0, pkt.length, pkt.length);
          fwrite(phdr_buf, 1, phdr_len, pcap_file);
          fwrite(pkt.payload, 1, pkt.length, pcap_file);
          fflush(pcap_file);
        }
      } else {
        if (!infinite) {
          timeouts++;
          if (hop_interval_ms == 0 || timeouts >= 2) {
            printf("Timeout waiting for 802.11 packet\n");
            break;
          }
        }
      }
    }

    if (pcap_file) {
      fclose(pcap_file);
      printf("PCAP capture saved successfully to %s\n", pcap_path);
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

    uint8_t buf[512];
    ssize_t byte_len = eos_net_hex_to_bytes(hex, buf, sizeof(buf));
    if (byte_len <= 0) {
      fprintf(stderr, "wlanctl: invalid hex string (max 512 bytes)\n");
      return 1;
    }

    int fd = open_wlan(dev);
    if (fd < 0) {
      perror("wlanctl: failed to open device");
      return 1;
    }

    ssize_t sent = write(fd, buf, (size_t)byte_len);
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

    eos_wifi_scan_config_t scfg = {
        .mode = passive ? EOS_WIFI_SCAN_PASSIVE : EOS_WIFI_SCAN_ACTIVE,
        .channel = channel,
    };

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
          char bssid_str[18];
          eos_net_format_mac(res.aps[i].bssid, bssid_str, sizeof(bssid_str));
          printf("%-32s  %-17s  %4d  %2u  %-12s\n",
                 ssid, bssid_str,
                 res.aps[i].rssi, (unsigned)res.aps[i].channel,
                 res.aps[i].auth_name[0] ? res.aps[i].auth_name : "UNKNOWN");
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
