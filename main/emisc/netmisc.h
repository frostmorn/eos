#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
// Network Math, Conversion, and Protocol Helpers
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── IPv4 & Netmask Math ───────────────────────────────────────────────────

static inline uint32_t eos_net_ipv4_to_u32(const char *ip_str) {
  if (!ip_str || !ip_str[0]) return 0;
  unsigned int a = 0, b = 0, c = 0, d = 0;
  if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
    return ((uint32_t)(a & 0xFF) << 24) |
           ((uint32_t)(b & 0xFF) << 16) |
           ((uint32_t)(c & 0xFF) << 8)  |
           (uint32_t)(d & 0xFF);
  }
  return 0;
}

static inline void eos_net_u32_to_ipv4(uint32_t ip, char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  snprintf(out, out_len, "%u.%u.%u.%u",
           (unsigned)((ip >> 24) & 0xFF),
           (unsigned)((ip >> 16) & 0xFF),
           (unsigned)((ip >> 8) & 0xFF),
           (unsigned)(ip & 0xFF));
}

static inline uint32_t eos_net_prefix_to_u32(int prefix) {
  if (prefix <= 0) return 0;
  if (prefix >= 32) return 0xFFFFFFFFU;
  return ~0U << (32 - prefix);
}

static inline int eos_net_u32_to_prefix(uint32_t mask) {
  int count = 0;
  while (mask & 0x80000000U) {
    count++;
    mask <<= 1;
  }
  return count;
}

static inline int eos_net_mask_to_prefix(const char *mask_str) {
  if (!mask_str || !mask_str[0]) return 24;
  if (strchr(mask_str, '.')) {
    uint32_t m = eos_net_ipv4_to_u32(mask_str);
    return eos_net_u32_to_prefix(m);
  }
  int p = atoi(mask_str);
  if (p > 0 && p <= 32) return p;
  return 24;
}

static inline void eos_net_prefix_to_mask(int prefix, char *out, size_t out_len) {
  if (prefix < 0) prefix = 0;
  if (prefix > 32) prefix = 32;
  uint32_t m = eos_net_prefix_to_u32(prefix);
  eos_net_u32_to_ipv4(m, out, out_len);
}

static inline void eos_net_calc_broadcast(const char *ip_str, const char *mask_str, char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  uint32_t ip = eos_net_ipv4_to_u32(ip_str);
  uint32_t mask = eos_net_ipv4_to_u32(mask_str);
  if (mask == 0) mask = 0xFFFFFF00U; // Default 255.255.255.0
  uint32_t bcast = (ip & mask) | (~mask);
  eos_net_u32_to_ipv4(bcast, out, out_len);
}

static inline void eos_net_calc_default_gw(const char *ip_str, char *out_gw, size_t gw_len) {
  if (!out_gw || gw_len == 0) return;
  if (!ip_str || !ip_str[0]) {
    strncpy(out_gw, "0.0.0.0", gw_len - 1);
    out_gw[gw_len - 1] = '\0';
    return;
  }
  strncpy(out_gw, ip_str, gw_len - 1);
  out_gw[gw_len - 1] = '\0';
  char *dot = strrchr(out_gw, '.');
  if (dot) {
    size_t remaining = gw_len - (size_t)(dot + 1 - out_gw);
    snprintf(dot + 1, remaining, "1");
  }
}

static inline bool eos_net_parse_cidr(const char *cidr, char *out_ip, size_t ip_len,
                                      char *out_mask, size_t mask_len, int *out_prefix) {
  if (!cidr || !out_ip || !out_mask) return false;
  const char *slash = strchr(cidr, '/');
  if (slash) {
    size_t iplen = (size_t)(slash - cidr);
    if (iplen >= ip_len) iplen = ip_len - 1;
    strncpy(out_ip, cidr, iplen);
    out_ip[iplen] = '\0';
    int p = atoi(slash + 1);
    if (p < 0) p = 0;
    if (p > 32) p = 32;
    if (out_prefix) *out_prefix = p;
    eos_net_prefix_to_mask(p, out_mask, mask_len);
  } else {
    strncpy(out_ip, cidr, ip_len - 1);
    out_ip[ip_len - 1] = '\0';
    if (out_prefix) *out_prefix = 24;
    strncpy(out_mask, "255.255.255.0", mask_len - 1);
    out_mask[mask_len - 1] = '\0';
  }
  return true;
}

// ── MAC Address Math & Formatting ─────────────────────────────────────────

static inline bool eos_net_parse_mac(const char *str, uint8_t mac[6]) {
  if (!str || !mac) return false;
  unsigned int m[6];
  if (sscanf(str, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6 ||
      sscanf(str, "%x-%x-%x-%x-%x-%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
    for (int i = 0; i < 6; i++) {
      mac[i] = (uint8_t)(m[i] & 0xFF);
    }
    return true;
  }
  return false;
}

static inline void eos_net_format_mac(const uint8_t mac[6], char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  if (!mac) {
    snprintf(out, out_len, "00:00:00:00:00:00");
    return;
  }
  snprintf(out, out_len, "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ── Hex Conversion Helpers ────────────────────────────────────────────────

static inline ssize_t eos_net_hex_to_bytes(const char *hex, uint8_t *bytes, size_t max_len) {
  if (!hex || !bytes) return -1;
  size_t hex_len = strlen(hex);
  if (hex_len % 2 != 0) return -1;
  size_t byte_len = hex_len / 2;
  if (byte_len > max_len) return -1;

  for (size_t i = 0; i < byte_len; i++) {
    char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char *endptr = NULL;
    unsigned long val = strtoul(byte_str, &endptr, 16);
    if (!endptr || *endptr != '\0') return -1;
    bytes[i] = (uint8_t)(val & 0xFF);
  }
  return (ssize_t)byte_len;
}

static inline size_t eos_net_bytes_to_hex(const uint8_t *bytes, size_t len, char *out_hex, size_t max_len) {
  if (!bytes || !out_hex || max_len < (len * 2 + 1)) return 0;
  static const char hex_chars[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out_hex[i * 2] = hex_chars[(bytes[i] >> 4) & 0xF];
    out_hex[i * 2 + 1] = hex_chars[bytes[i] & 0xF];
  }
  out_hex[len * 2] = '\0';
  return len * 2;
}

// ── WiFi & 802.11 Helpers ──────────────────────────────────────────────────

static inline const char *eos_net_wifi_auth_name(uint8_t authmode) {
  static const char *names[] = {
      "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA/WPA2", "WPA3_PSK", "WPA2/WPA3", "WAPI_PSK", "UNKNOWN"
  };
  if (authmode < sizeof(names) / sizeof(names[0]) - 1) {
    return names[authmode];
  }
  return "UNKNOWN";
}

static inline int eos_net_rssi_to_quality(int8_t rssi) {
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
}

// ── PCAP File Format & Packet Filtering Helpers ───────────────────────────

typedef struct __attribute__((packed)) {
  uint32_t magic_number;   /* magic number */
  uint16_t version_major;  /* major version number */
  uint16_t version_minor;  /* minor version number */
  int32_t  thiszone;       /* GMT to local correction */
  uint32_t sigfigs;        /* accuracy of timestamps */
  uint32_t snaplen;        /* max length of captured packets, in octets */
  uint32_t network;        /* data link type (80 = LINKTYPE_IEEE802_11) */
} eos_pcap_hdr_t;

typedef struct __attribute__((packed)) {
  uint32_t ts_sec;         /* timestamp seconds */
  uint32_t ts_usec;        /* timestamp microseconds */
  uint32_t incl_len;       /* number of octets of packet saved in file */
  uint32_t orig_len;       /* actual length of packet on the wire */
} eos_pcap_packet_hdr_t;

static inline size_t eos_net_pcap_write_global_header(uint8_t *buf, size_t max_len) {
  if (!buf || max_len < sizeof(eos_pcap_hdr_t)) return 0;
  eos_pcap_hdr_t *hdr = (eos_pcap_hdr_t *)buf;
  hdr->magic_number = 0xa1b2c3d4;
  hdr->version_major = 2;
  hdr->version_minor = 4;
  hdr->thiszone = 0;
  hdr->sigfigs = 0;
  hdr->snaplen = 65535;
  hdr->network = 80; // LINKTYPE_IEEE802_11
  return sizeof(eos_pcap_hdr_t);
}

static inline size_t eos_net_pcap_write_packet_header(uint8_t *buf, size_t max_len,
                                                      uint32_t sec, uint32_t usec,
                                                      uint32_t incl_len, uint32_t orig_len) {
  if (!buf || max_len < sizeof(eos_pcap_packet_hdr_t)) return 0;
  eos_pcap_packet_hdr_t *phdr = (eos_pcap_packet_hdr_t *)buf;
  phdr->ts_sec = sec;
  phdr->ts_usec = usec;
  phdr->incl_len = incl_len;
  phdr->orig_len = orig_len;
  return sizeof(eos_pcap_packet_hdr_t);
}

static inline bool eos_net_wifi_match_mac(const uint8_t pkt_mac[6], const uint8_t filter_mac[6]) {
  if (!pkt_mac || !filter_mac) return false;
  for (int i = 0; i < 6; i++) {
    if (pkt_mac[i] != filter_mac[i]) return false;
  }
  return true;
}

static inline bool eos_net_wifi_match_type(const char *type_name, const char *filter_type) {
  if (!type_name || !filter_type) return true;
  if (strcasecmp(filter_type, "all") == 0 || filter_type[0] == '\0') return true;
  return (strcasecmp(type_name, filter_type) == 0);
}

#ifdef __cplusplus
}
#endif
