#include "includes.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/netif.h"
#include "emisc/netmisc.h"
#include "ebin/wlanctl/wlanctl.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int open_wlan(const char *name) {
  if (!name || !name[0]) {
    name = "wifi0";
  }
  char path[EOS_SMALL_STR_LEN];
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

static int cmd_help(int argc, char **argv) {
  (void)argc; (void)argv;
  print_usage();
  return 0;
}

static int cmd_radio(int argc, char **argv) {
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

static int cmd_getmac(int argc, char **argv) {
  const char *dev = (argc > 2) ? argv[2] : "wifi0";
  int fd = open_wlan(dev);
  if (fd < 0) {
    perror("wlanctl: failed to open device");
    return 1;
  }

  uint8_t mac[6] = {0};
  if (ioctl(fd, EOS_NETIF_IOCTL_GET_MAC, mac) == 0) {
    char mac_str[EOS_STR_20_LEN];
    eos_net_format_mac(mac, mac_str, sizeof(mac_str));
    printf("%s: MAC address %s\n", dev, mac_str);
    close(fd);
    return 0;
  }

  perror("wlanctl: get MAC failed");
  close(fd);
  return 1;
}

static int cmd_setmac(int argc, char **argv) {
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

static int cmd_channel(int argc, char **argv) {
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

static int cmd_sniffer(int argc, char **argv) {
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

static void parse_dump_args(int argc, char **argv, wlan_dump_opts_t *opts) {
  opts->count = 10;
  opts->dev = "wifi0";
  opts->type_filter = NULL;
  opts->has_bssid = false;
  opts->has_src = false;
  opts->has_dst = false;
  opts->hop_interval_ms = 0;
  opts->verbose = false;
  opts->pcap_path = NULL;

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
      opts->type_filter = argv[++i];
    } else if (strcmp(argv[i], "--bssid") == 0 && i + 1 < argc) {
      opts->has_bssid = eos_net_parse_mac(argv[++i], opts->bssid_filter);
    } else if (strcmp(argv[i], "--src") == 0 && i + 1 < argc) {
      opts->has_src = eos_net_parse_mac(argv[++i], opts->src_filter);
    } else if (strcmp(argv[i], "--dst") == 0 && i + 1 < argc) {
      opts->has_dst = eos_net_parse_mac(argv[++i], opts->dst_filter);
    } else if (strcmp(argv[i], "--hop") == 0 && i + 1 < argc) {
      opts->hop_interval_ms = (uint32_t)atoi(argv[++i]);
    } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
      opts->verbose = true;
    } else if (strcmp(argv[i], "--pcap") == 0 && i + 1 < argc) {
      opts->pcap_path = argv[++i];
    } else if (argv[i][0] != '-') {
      if (atoi(argv[i]) > 0 || strcmp(argv[i], "0") == 0) {
        opts->count = atoi(argv[i]);
      } else if (strncmp(argv[i], "wifi", 4) == 0 || argv[i][0] == '/') {
        opts->dev = argv[i];
      }
    }
  }
}

static void dump_display_packet(int displayed, const eos_wifi_raw_packet_t *pkt, bool verbose) {
  printf("[%3d] CH %2u | RSSI %4d dBm | Len %4u | %s (sub %u)\n",
         displayed, pkt->channel, pkt->rssi, pkt->length,
         pkt->type_name[0] ? pkt->type_name : "UNKNOWN",
         pkt->frame_subtype);
  if (pkt->has_addrs) {
    char a1[EOS_STR_20_LEN], a2[EOS_STR_20_LEN], a3[EOS_STR_20_LEN];
    eos_net_format_mac(pkt->addr1, a1, sizeof(a1));
    eos_net_format_mac(pkt->addr2, a2, sizeof(a2));
    eos_net_format_mac(pkt->addr3, a3, sizeof(a3));
    printf("      DA (Addr1): %s  SA (Addr2): %s  BSSID (Addr3): %s\n", a1, a2, a3);
  }
  if (verbose && pkt->length > 0) {
    printf("      Payload hex (%u bytes):\n      ", pkt->length);
    for (uint16_t j = 0; j < pkt->length && j < EOS_LINE_64_LEN; j++) {
      printf("%02x ", pkt->payload[j]);
      if ((j + 1) % EOS_XSMALL_STR_LEN == 0) printf("\n      ");
    }
    printf("\n");
  }
}

static int cmd_dump(int argc, char **argv) {
  wlan_dump_opts_t opts;
  parse_dump_args(argc, argv, &opts);

  int fd = open_wlan(opts.dev);
  if (fd < 0) {
    perror("wlanctl: failed to open device");
    return 1;
  }

  eos_wifi_promisc_cfg_t pcfg = {.enable = true};
  ioctl(fd, EOS_RADIO_IOCTL_SET_PROMISC, &pcfg);

  FILE *pcap_file = NULL;
  if (opts.pcap_path) {
    pcap_file = fopen(opts.pcap_path, "wb");
    if (pcap_file) {
      uint8_t hdr_buf[EOS_SMALL_STR_LEN];
      size_t hdr_len = eos_net_pcap_write_global_header(hdr_buf, sizeof(hdr_buf));
      fwrite(hdr_buf, 1, hdr_len, pcap_file);
      printf("Saving captured frames to PCAP file: %s\n", opts.pcap_path);
    } else {
      perror("wlanctl: failed to open PCAP file for writing");
    }
  }

  bool infinite = (opts.count <= 0);
  if (infinite) {
    printf("Capturing infinite 802.11 frames from %s%s%s%s...\n\n",
           opts.dev,
           opts.type_filter ? " [type=" : "", opts.type_filter ? opts.type_filter : "", opts.type_filter ? "]" : "");
  } else {
    printf("Capturing %d raw 802.11 frames from %s%s%s%s...\n\n",
           opts.count, opts.dev,
           opts.type_filter ? " [type=" : "", opts.type_filter ? opts.type_filter : "", opts.type_filter ? "]" : "");
  }

  int captured = 0;
  int displayed = 0;
  uint8_t current_channel = 1;
  int timeouts = 0;

  while (infinite || displayed < opts.count) {
    if (opts.hop_interval_ms > 0) {
      current_channel++;
      if (current_channel > 13) current_channel = 1;
      ioctl(fd, EOS_RADIO_IOCTL_SET_CHANNEL, (int)current_channel);
      usleep(opts.hop_interval_ms * EOS_US_PER_MS);
    }

    eos_wifi_raw_packet_t pkt;
    ssize_t n = read(fd, &pkt, sizeof(pkt));
    if (n > 0) {
      captured++;
      if (!eos_net_wifi_match_type(pkt.type_name, opts.type_filter)) continue;
      if (opts.has_bssid && (!pkt.has_addrs || !eos_net_wifi_match_mac(pkt.addr3, opts.bssid_filter))) continue;
      if (opts.has_src && (!pkt.has_addrs || !eos_net_wifi_match_mac(pkt.addr2, opts.src_filter))) continue;
      if (opts.has_dst && (!pkt.has_addrs || !eos_net_wifi_match_mac(pkt.addr1, opts.dst_filter))) continue;

      displayed++;
      dump_display_packet(displayed, &pkt, opts.verbose);

      if (pcap_file) {
        uint8_t phdr_buf[EOS_XSMALL_STR_LEN];
        size_t phdr_len = eos_net_pcap_write_packet_header(phdr_buf, sizeof(phdr_buf),
                                                           0, 0, pkt.length, pkt.length);
        fwrite(phdr_buf, 1, phdr_len, pcap_file);
        fwrite(pkt.payload, 1, pkt.length, pcap_file);
        fflush(pcap_file);
      }
    } else {
      if (!infinite) {
        timeouts++;
        if (opts.hop_interval_ms == 0 || timeouts >= 2) {
          printf("Timeout waiting for 802.11 packet\n");
          break;
        }
      }
    }
  }

  if (pcap_file) {
    fclose(pcap_file);
    printf("PCAP capture saved successfully to %s\n", opts.pcap_path);
  }

  close(fd);
  return 0;
}

static int cmd_send(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: wlanctl send <hex-bytes> [dev]\n");
    return 1;
  }
  const char *hex = argv[2];
  const char *dev = (argc > 3) ? argv[3] : "wifi0";

  uint8_t buf[EOS_SECTOR_SIZE];
  ssize_t byte_len = eos_net_hex_to_bytes(hex, buf, sizeof(buf));
  if (byte_len <= 0) {
    fprintf(stderr, "wlanctl: invalid hex string (max %d bytes)\n", EOS_SECTOR_SIZE);
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

static int cmd_scan(int argc, char **argv) {
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
        char bssid_str[EOS_STR_20_LEN];
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

typedef struct {
  const char *name;
  const char *alt;
  int (*handler)(int argc, char **argv);
} wlanctl_cmd_t;

static const wlanctl_cmd_t WLANCTL_CMDS[] = {
  {"help", "-h", cmd_help},
  {"--help", NULL, cmd_help},
  {"radio", NULL, cmd_radio},
  {"getmac", NULL, cmd_getmac},
  {"setmac", NULL, cmd_setmac},
  {"channel", NULL, cmd_channel},
  {"sniffer", "promisc", cmd_sniffer},
  {"dump", NULL, cmd_dump},
  {"send", NULL, cmd_send},
  {"scan", NULL, cmd_scan},
};

int wlanctl_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 0;
  }

  const char *cmd = argv[1];
  for (size_t i = 0; i < sizeof(WLANCTL_CMDS) / sizeof(WLANCTL_CMDS[0]); i++) {
    if (strcmp(cmd, WLANCTL_CMDS[i].name) == 0 ||
        (WLANCTL_CMDS[i].alt && strcmp(cmd, WLANCTL_CMDS[i].alt) == 0)) {
      return WLANCTL_CMDS[i].handler(argc, argv);
    }
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
