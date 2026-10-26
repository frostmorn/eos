#include "includes.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/netif.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define NETCTL_MAX_IFACES 16

static const char *s_default_ifaces[] = {
    "sta0", "ap0", "wifi0", "br0", "eth0", NULL
};

static int open_iface(const char *name) {
  if (!name) return -1;
  char path[64];
  if (name[0] == '/') {
    snprintf(path, sizeof(path), "%s", name);
  } else {
    snprintf(path, sizeof(path), "/dev/%s", name);
  }

  int fd = open(path, O_RDWR);
  if (fd < 0 && !strchr(name, '/')) {
    snprintf(path, sizeof(path), "/dev/%s0", name);
    fd = open(path, O_RDWR);
  }
  return fd;
}

static int discover_net_ifaces(char ifaces[NETCTL_MAX_IFACES][16]) {
  int count = 0;
  FILE *f = fopen("/proc/devices", "r");
  if (f) {
    char line[128];
    while (fgets(line, sizeof(line), f) && count < NETCTL_MAX_IFACES) {
      int id;
      char name[32], driver[32], parent[32];
      if (sscanf(line, "%d %31s %31s %31s", &id, name, driver, parent) >= 3) {
        if (strncmp(driver, "net/", 4) == 0) {
          strncpy(ifaces[count], name, 15);
          ifaces[count][15] = '\0';
          count++;
        }
      }
    }
    fclose(f);
  }

  if (count == 0) {
    for (int i = 0; s_default_ifaces[i] != NULL && count < NETCTL_MAX_IFACES; i++) {
      char path[32];
      snprintf(path, sizeof(path), "/dev/%s", s_default_ifaces[i]);
      if (access(path, F_OK) == 0) {
        strncpy(ifaces[count], s_default_ifaces[i], 15);
        ifaces[count][15] = '\0';
        count++;
      }
    }
  }

  return count;
}

// ── IP-style Display Formats ───────────────────────────────────────────────

static void print_link_one(int idx, const char *name) {
  int fd = open_iface(name);
  if (fd < 0) return;

  eos_netif_info_t info;
  memset(&info, 0, sizeof(info));
  if (ioctl(fd, EOS_NETIF_IOCTL_GET_INFO, &info) != 0) {
    close(fd);
    return;
  }
  close(fd);

  bool is_up = (info.flags & EOS_NETIF_FLAG_UP) != 0;
  bool is_running = (info.flags & EOS_NETIF_FLAG_LINK_UP) != 0;

  printf("%d: %s: <", idx, info.name[0] ? info.name : name);
  bool first = true;
  if (is_up) {
    printf("%sUP", first ? "" : ",");
    first = false;
  }
  if (is_running) {
    printf("%sLOWER_UP", first ? "" : ",");
    first = false;
  }
  if (info.flags & EOS_NETIF_FLAG_PROMISC) {
    printf("%sPROMISC", first ? "" : ",");
    first = false;
  }
  printf("%sBROADCAST,MULTICAST> mtu 1500 qdisc noqueue state %s mode DEFAULT\n",
         first ? "" : ",", is_up ? (is_running ? "UP" : "UNKNOWN") : "DOWN");

  const char *link_type = (info.type == EOS_NETIF_TYPE_RADIO) ? "link/ieee802.11" : "link/ether";
  printf("    %s %02x:%02x:%02x:%02x:%02x:%02x brd ff:ff:ff:ff:ff:ff\n",
         link_type,
         info.mac[0], info.mac[1], info.mac[2],
         info.mac[3], info.mac[4], info.mac[5]);
}

static void print_addr_one(int idx, const char *name) {
  int fd = open_iface(name);
  if (fd < 0) return;

  eos_netif_info_t info;
  memset(&info, 0, sizeof(info));
  if (ioctl(fd, EOS_NETIF_IOCTL_GET_INFO, &info) != 0) {
    close(fd);
    return;
  }
  close(fd);

  bool is_up = (info.flags & EOS_NETIF_FLAG_UP) != 0;
  bool is_running = (info.flags & EOS_NETIF_FLAG_LINK_UP) != 0;

  printf("%d: %s: <", idx, info.name[0] ? info.name : name);
  bool first = true;
  if (is_up) {
    printf("%sUP", first ? "" : ",");
    first = false;
  }
  if (is_running) {
    printf("%sLOWER_UP", first ? "" : ",");
    first = false;
  }
  if (info.flags & EOS_NETIF_FLAG_PROMISC) {
    printf("%sPROMISC", first ? "" : ",");
    first = false;
  }
  printf("%sBROADCAST,MULTICAST> mtu 1500 qdisc noqueue state %s\n",
         first ? "" : ",", is_up ? (is_running ? "UP" : "UNKNOWN") : "DOWN");

  const char *link_type = (info.type == EOS_NETIF_TYPE_RADIO) ? "link/ieee802.11" : "link/ether";
  printf("    %s %02x:%02x:%02x:%02x:%02x:%02x brd ff:ff:ff:ff:ff:ff\n",
         link_type,
         info.mac[0], info.mac[1], info.mac[2],
         info.mac[3], info.mac[4], info.mac[5]);

  if (info.type != EOS_NETIF_TYPE_RADIO && info.ip.ip[0] &&
      strcmp(info.ip.ip, "0.0.0.0") != 0) {
    int prefix = eos_net_mask_to_prefix(info.ip.netmask);
    char bcast[16] = {0};
    eos_net_calc_broadcast(info.ip.ip, info.ip.netmask, bcast, sizeof(bcast));
    const char *scope = (info.flags & EOS_NETIF_FLAG_DHCP_CLIENT) ? "global dynamic" : "global";
    printf("    inet %s/%d brd %s scope %s %s\n",
           info.ip.ip, prefix,
           bcast[0] ? bcast : (info.ip.gw[0] ? info.ip.gw : "0.0.0.0"),
           scope, info.name[0] ? info.name : name);
    printf("       valid_lft forever preferred_lft forever\n");
  }

  if (info.extra[0]) {
    printf("    /* %s */\n", info.extra);
  }
}

// ── IP-style Subcommands ───────────────────────────────────────────────────

static int cmd_link_show(int argc, char **argv) {
  const char *target = NULL;
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
      target = argv[++i];
    } else if (strcmp(argv[i], "show") != 0 && strcmp(argv[i], "list") != 0) {
      target = argv[i];
    }
  }

  if (target) {
    print_link_one(1, target);
    return 0;
  }

  char ifaces[NETCTL_MAX_IFACES][16];
  int count = discover_net_ifaces(ifaces);
  for (int i = 0; i < count; i++) {
    print_link_one(i + 1, ifaces[i]);
  }
  return 0;
}

static int cmd_link_set(int argc, char **argv) {
  const char *dev = NULL;
  const char *action = NULL;
  const char *arg = NULL;

  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
      dev = argv[++i];
    } else if (!dev) {
      dev = argv[i];
    } else if (!action) {
      action = argv[i];
    } else if (!arg) {
      arg = argv[i];
    }
  }

  if (!dev || !action) {
    fprintf(stderr, "Usage: netctl link set [dev] <interface> { up | down | master <bridge> | nomaster }\n");
    return 1;
  }

  int fd = open_iface(dev);
  if (fd < 0) {
    fprintf(stderr, "Cannot find device \"%s\"\n", dev);
    return 1;
  }

  if (strcmp(action, "up") == 0) {
    if (ioctl(fd, EOS_NETIF_IOCTL_SET_UP) == 0) {
      close(fd);
      return 0;
    } else {
      perror("RTNETLINK answers: Operation not permitted");
      close(fd);
      return 1;
    }
  } else if (strcmp(action, "down") == 0) {
    if (ioctl(fd, EOS_NETIF_IOCTL_SET_DOWN) == 0) {
      close(fd);
      return 0;
    } else {
      perror("RTNETLINK answers: Operation not permitted");
      close(fd);
      return 1;
    }
  } else if (strcmp(action, "master") == 0) {
    close(fd);
    if (!arg) {
      fprintf(stderr, "Missing bridge name for 'master'\n");
      return 1;
    }
    int bfd = open_iface(arg);
    if (bfd < 0) {
      fprintf(stderr, "Cannot find bridge device \"%s\"\n", arg);
      return 1;
    }
    if (ioctl(bfd, EOS_NETIF_IOCTL_BRIDGE_ADD_IF, dev) == 0) {
      close(bfd);
      return 0;
    } else {
      perror("RTNETLINK answers: Failed to add port to bridge");
      close(bfd);
      return 1;
    }
  } else if (strcmp(action, "nomaster") == 0) {
    close(fd);
    int bfd = open_iface("br0");
    if (bfd >= 0) {
      ioctl(bfd, EOS_NETIF_IOCTL_BRIDGE_DEL_IF, dev);
      close(bfd);
    }
    return 0;
  } else {
    fprintf(stderr, "Unknown command \"%s\"\n", action);
    close(fd);
    return 1;
  }
}

static int cmd_addr_show(int argc, char **argv) {
  const char *target = NULL;
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
      target = argv[++i];
    } else if (strcmp(argv[i], "show") != 0 && strcmp(argv[i], "list") != 0) {
      target = argv[i];
    }
  }

  if (target) {
    print_addr_one(1, target);
    return 0;
  }

  char ifaces[NETCTL_MAX_IFACES][16];
  int count = discover_net_ifaces(ifaces);
  for (int i = 0; i < count; i++) {
    print_addr_one(i + 1, ifaces[i]);
  }
  return 0;
}

static int cmd_addr_add(int argc, char **argv) {
  const char *cidr = NULL;
  const char *dev = NULL;
  const char *gw = NULL;

  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "dev") == 0 && i + 1 < argc) {
      dev = argv[++i];
    } else if (strcmp(argv[i], "gw") == 0 || strcmp(argv[i], "gateway") == 0) {
      if (i + 1 < argc) gw = argv[++i];
    } else if (!cidr) {
      cidr = argv[i];
    } else if (!dev) {
      dev = argv[i];
    }
  }

  if (!cidr || !dev) {
    fprintf(stderr, "Usage: netctl addr add <ip/prefix> dev <interface> [gw <gateway>]\n");
    return 1;
  }

  eos_netif_ip_info_t ip_info;
  memset(&ip_info, 0, sizeof(ip_info));
  if (!eos_net_parse_cidr(cidr, ip_info.ip, sizeof(ip_info.ip), ip_info.netmask, sizeof(ip_info.netmask), NULL)) {
    fprintf(stderr, "netctl: invalid IP or CIDR address \"%s\"\n", cidr);
    return 1;
  }
  if (gw) {
    strncpy(ip_info.gw, gw, sizeof(ip_info.gw) - 1);
  } else {
    eos_net_calc_default_gw(ip_info.ip, ip_info.gw, sizeof(ip_info.gw));
  }

  int fd = open_iface(dev);
  if (fd < 0) {
    fprintf(stderr, "Cannot find device \"%s\"\n", dev);
    return 1;
  }

  if (ioctl(fd, EOS_NETIF_IOCTL_SET_IP, &ip_info) == 0) {
    close(fd);
    return 0;
  } else {
    perror("RTNETLINK answers: Invalid argument");
    close(fd);
    return 1;
  }
}

// ── Status and Script Reporting ───────────────────────────────────────────

static int cmd_status_line(const char *dev) {
  int fd = open_iface(dev);
  if (fd < 0) {
    printf("STATUS:if=%s,up=0,link=0,ip=0.0.0.0,mask=0.0.0.0,gw=0.0.0.0,mac=00:00:00:00:00:00,extra=not_found\n", dev);
    return 1;
  }

  eos_netif_info_t info;
  memset(&info, 0, sizeof(info));
  if (ioctl(fd, EOS_NETIF_IOCTL_GET_INFO, &info) != 0) {
    close(fd);
    printf("STATUS:if=%s,up=0,link=0,ip=0.0.0.0,mask=0.0.0.0,gw=0.0.0.0,mac=00:00:00:00:00:00,extra=error\n", dev);
    return 1;
  }
  close(fd);

  int up = (info.flags & EOS_NETIF_FLAG_UP) ? 1 : 0;
  int link = (info.flags & EOS_NETIF_FLAG_LINK_UP) ? 1 : 0;

  printf("STATUS:if=%s,up=%d,link=%d,ip=%s,mask=%s,gw=%s,mac=%02x:%02x:%02x:%02x:%02x:%02x,extra=%s\n",
         info.name[0] ? info.name : dev, up, link,
         info.ip.ip[0] ? info.ip.ip : "0.0.0.0",
         info.ip.netmask[0] ? info.ip.netmask : "0.0.0.0",
         info.ip.gw[0] ? info.ip.gw : "0.0.0.0",
         info.mac[0], info.mac[1], info.mac[2],
         info.mac[3], info.mac[4], info.mac[5],
         info.extra[0] ? info.extra : "none");
  return 0;
}

static void print_usage(void) {
  printf("Usage: netctl [ OBJECT | COMMAND ] [ args... ]\n\n"
         "IP-style Objects (ip vibe):\n"
         "  netctl link [show [dev]]            Show interface links and MAC addresses\n"
         "  netctl link set <dev> up|down       Bring interface up or down\n"
         "  netctl link set <dev> master <br>   Attach interface to bridge (e.g. master br0)\n"
         "  netctl link set <dev> nomaster      Detach interface from bridge\n"
         "  netctl addr [show [dev]]            Show IP addresses (or 'netctl a')\n"
         "  netctl addr add <ip/pfx> dev <dev>  Configure static IPv4 address\n\n"
         "Shortcuts & Interface Commands:\n"
         "  netctl <iface> up | down            Bring interface up or down\n"
         "  netctl up <iface> | down <iface>    Alternative up/down shortcut\n"
         "  netctl <iface> status               Print machine-readable status line for GUI\n"
         "  netctl <iface> ip <ip> [mask] [gw]  Set static IPv4 address\n"
         "  netctl <iface> dhcp start|stop      Start/stop DHCP client or server\n"
         "  netctl <iface> connect <ssid> [pw]  Connect STA interface to WiFi network\n"
         "  netctl <iface> disconnect           Disconnect STA from network\n"
         "  netctl <iface> ap <ssid> [pw] [ch]  Configure SoftAP settings\n"
         "  netctl <iface> clients              List connected stations on AP\n"
         "  netctl <br> add <port> | del <port> Attach or detach bridge port\n"
         "  netctl <br> napt on|off             Enable/disable NAPT routing\n");
}

int netctl_main(int argc, char **argv) {
  // netctl with no args: display addr table (like 'ip a' on Linux)
  if (argc <= 1) {
    return cmd_addr_show(0, NULL);
  }

  const char *arg1 = argv[1];

  if (strcmp(arg1, "-h") == 0 || strcmp(arg1, "--help") == 0 || strcmp(arg1, "help") == 0) {
    print_usage();
    return 0;
  }

  // ── ip-style: netctl link / l ──
  if (strcmp(arg1, "link") == 0 || strcmp(arg1, "l") == 0) {
    if (argc < 3 || strcmp(argv[2], "show") == 0 || strcmp(argv[2], "list") == 0) {
      return cmd_link_show(argc - 2, argv + 2);
    }
    if (strcmp(argv[2], "set") == 0) {
      return cmd_link_set(argc - 3, argv + 3);
    }
    fprintf(stderr, "Unknown link command \"%s\"\n", argv[2]);
    return 1;
  }

  // ── ip-style: netctl addr / a / address ──
  if (strcmp(arg1, "addr") == 0 || strcmp(arg1, "a") == 0 || strcmp(arg1, "address") == 0) {
    if (argc < 3 || strcmp(argv[2], "show") == 0 || strcmp(argv[2], "list") == 0) {
      return cmd_addr_show(argc - 2, argv + 2);
    }
    if (strcmp(argv[2], "add") == 0) {
      return cmd_addr_add(argc - 3, argv + 3);
    }
    fprintf(stderr, "Unknown addr command \"%s\"\n", argv[2]);
    return 1;
  }

  // ── Shortcuts: netctl up <dev> / netctl down <dev> ──
  if ((strcmp(arg1, "up") == 0 || strcmp(arg1, "down") == 0) && argc >= 3) {
    char *sub_argv[] = {(char *)argv[2], (char *)arg1};
    return cmd_link_set(2, sub_argv);
  }

  // ── Interface-first commands: netctl <dev> ... ──
  const char *dev = arg1;

  if (argc == 2) {
    // netctl <dev> -> show addr for this dev
    return print_addr_one(1, dev), 0;
  }

  const char *cmd = argv[2];

  // netctl <dev> up / down
  if (strcmp(cmd, "up") == 0 || strcmp(cmd, "down") == 0) {
    char *sub_argv[] = {(char *)dev, (char *)cmd};
    return cmd_link_set(2, sub_argv);
  }

  // netctl <dev> status (machine-readable for GUI)
  if (strcmp(cmd, "status") == 0) {
    return cmd_status_line(dev);
  }

  // netctl <dev> ip <ip> [mask] [gw]
  if (strcmp(cmd, "ip") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s ip <ip_addr> [netmask] [gateway]\n", dev);
      return 1;
    }
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open interface");
      return 1;
    }
    eos_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    strncpy(ip_info.ip, argv[3], sizeof(ip_info.ip) - 1);
    if (argc > 4) {
      if (strchr(argv[4], '.')) {
        strncpy(ip_info.netmask, argv[4], sizeof(ip_info.netmask) - 1);
      } else {
        eos_net_prefix_to_mask(atoi(argv[4]), ip_info.netmask, sizeof(ip_info.netmask));
      }
    } else {
      strncpy(ip_info.netmask, "255.255.255.0", sizeof(ip_info.netmask) - 1);
    }
    if (argc > 5) {
      strncpy(ip_info.gw, argv[5], sizeof(ip_info.gw) - 1);
    } else {
      eos_net_calc_default_gw(ip_info.ip, ip_info.gw, sizeof(ip_info.gw));
    }
    if (ioctl(fd, EOS_NETIF_IOCTL_SET_IP, &ip_info) == 0) {
      printf("%s: IP set to %s (mask: %s, gw: %s)\n", dev, ip_info.ip, ip_info.netmask, ip_info.gw);
      close(fd);
      return 0;
    } else {
      perror("netctl: set IP failed");
      close(fd);
      return 1;
    }
  }

  // netctl <dev> dhcp start|stop
  if (strcmp(cmd, "dhcp") == 0) {
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open interface");
      return 1;
    }
    bool start = (argc > 3 && strcmp(argv[3], "stop") == 0) ? false : true;
    int req = start ? EOS_NETIF_IOCTL_START_DHCPC : EOS_NETIF_IOCTL_STOP_DHCPC;
    if (ioctl(fd, req) == 0) {
      printf("%s: DHCP %s\n", dev, start ? "started" : "stopped");
      close(fd);
      return 0;
    } else {
      perror("netctl: DHCP command failed");
      close(fd);
      return 1;
    }
  }

  // netctl <dev> connect <ssid> [pass]
  if (strcmp(cmd, "connect") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s connect <ssid> [password]\n", dev);
      return 1;
    }
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open sta interface");
      return 1;
    }
    eos_sta_connect_t conn;
    memset(&conn, 0, sizeof(conn));
    strncpy(conn.ssid, argv[3], sizeof(conn.ssid) - 1);
    if (argc > 4) {
      strncpy(conn.password, argv[4], sizeof(conn.password) - 1);
    }
    conn.timeout_ms = 10000;
    printf("%s: connecting to \"%s\"...\n", dev, conn.ssid);
    if (ioctl(fd, EOS_STA_IOCTL_CONNECT, &conn) == 0) {
      printf("%s: connected to \"%s\"\n", dev, conn.ssid);
      close(fd);
      return 0;
    } else {
      perror("netctl: connect failed");
      close(fd);
      return 1;
    }
  }

  // netctl <dev> disconnect
  if (strcmp(cmd, "disconnect") == 0) {
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open interface");
      return 1;
    }
    if (ioctl(fd, EOS_STA_IOCTL_DISCONNECT) == 0) {
      printf("%s: disconnected\n", dev);
      close(fd);
      return 0;
    } else {
      perror("netctl: disconnect failed");
      close(fd);
      return 1;
    }
  }

  // netctl <dev> ap <ssid> [pass] [ch]
  if (strcmp(cmd, "ap") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s ap <ssid> [password] [channel]\n", dev);
      return 1;
    }
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open ap interface");
      return 1;
    }
    eos_ap_config_t ap_cfg;
    memset(&ap_cfg, 0, sizeof(ap_cfg));
    strncpy(ap_cfg.ssid, argv[3], sizeof(ap_cfg.ssid) - 1);
    if (argc > 4 && strlen(argv[4]) > 0) {
      strncpy(ap_cfg.password, argv[4], sizeof(ap_cfg.password) - 1);
      ap_cfg.authmode = 3; // WPA2_PSK
    } else {
      ap_cfg.authmode = 0; // OPEN
    }
    ap_cfg.channel = (argc > 5) ? (uint8_t)atoi(argv[5]) : 1;
    ap_cfg.max_connection = 4;
    if (ioctl(fd, EOS_AP_IOCTL_SET_CONFIG, &ap_cfg) == 0) {
      printf("%s: AP configured (SSID: \"%s\", ch %d)\n", dev, ap_cfg.ssid, ap_cfg.channel);
      close(fd);
      return 0;
    } else {
      perror("netctl: configure AP failed");
      close(fd);
      return 1;
    }
  }

  // netctl <dev> clients
  if (strcmp(cmd, "clients") == 0) {
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open interface");
      return 1;
    }
    eos_ap_sta_list_t stas;
    memset(&stas, 0, sizeof(stas));
    if (ioctl(fd, EOS_AP_IOCTL_GET_STAS, &stas) == 0) {
      printf("%s: %d connected client(s)\n", dev, stas.count);
      for (int i = 0; i < stas.count && i < 8; i++) {
        printf("  [%d] MAC %02x:%02x:%02x:%02x:%02x:%02x  RSSI %d dBm\n",
               i + 1,
               stas.stas[i].mac[0], stas.stas[i].mac[1], stas.stas[i].mac[2],
               stas.stas[i].mac[3], stas.stas[i].mac[4], stas.stas[i].mac[5],
               stas.stas[i].rssi);
      }
      close(fd);
      return 0;
    } else {
      perror("netctl: get clients failed");
      close(fd);
      return 1;
    }
  }

  // netctl <br> add <port>
  if (strcmp(cmd, "add") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s add <port_iface>\n", dev);
      return 1;
    }
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open bridge interface");
      return 1;
    }
    if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_ADD_IF, argv[3]) == 0) {
      printf("%s: attached port \"%s\"\n", dev, argv[3]);
      close(fd);
      return 0;
    } else {
      perror("netctl: add port failed");
      close(fd);
      return 1;
    }
  }

  // netctl <br> del <port>
  if (strcmp(cmd, "del") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s del <port_iface>\n", dev);
      return 1;
    }
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open bridge interface");
      return 1;
    }
    if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_DEL_IF, argv[3]) == 0) {
      printf("%s: removed port \"%s\"\n", dev, argv[3]);
      close(fd);
      return 0;
    } else {
      perror("netctl: del port failed");
      close(fd);
      return 1;
    }
  }

  // netctl <br> napt on|off
  if (strcmp(cmd, "napt") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: netctl %s napt on|off\n", dev);
      return 1;
    }
    int fd = open_iface(dev);
    if (fd < 0) {
      perror("netctl: failed to open bridge interface");
      return 1;
    }
    bool enable = (strcmp(argv[3], "on") == 0);
    if (ioctl(fd, EOS_NETIF_IOCTL_BRIDGE_SET_NAPT, (int)enable) == 0) {
      printf("%s: NAPT %s\n", dev, enable ? "enabled" : "disabled");
      close(fd);
      return 0;
    } else {
      perror("netctl: set NAPT failed");
      close(fd);
      return 1;
    }
  }

  fprintf(stderr, "Unknown command \"%s\" for interface \"%s\". Try 'netctl help'.\n", cmd, dev);
  return 1;
}

EOS_BIN_ATTR const eos_bin_t netctl_bin = {
    EOS_BIN_INITIALIZER,
    .flags = 0,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "netctl",
    .name = "Network Control",
    .group = "Network",
    .description = "Unified network link and address configuration utility (ip vibe)",
    .entry_point = netctl_main,
};
