#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
// Network Interface & Wireless Control Definitions
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Netif interface flags
#define EOS_NETIF_FLAG_UP          (1U << 0) // Interface is administratively UP
#define EOS_NETIF_FLAG_LINK_UP     (1U << 1) // Physical link / association is up
#define EOS_NETIF_FLAG_DHCP_CLIENT (1U << 2) // DHCP client is running / enabled
#define EOS_NETIF_FLAG_DHCP_SERVER (1U << 3) // DHCP server is running / enabled
#define EOS_NETIF_FLAG_BRIDGE      (1U << 4) // Interface is a bridge
#define EOS_NETIF_FLAG_PROMISC     (1U << 5) // Promiscuous mode enabled

// Netif interface types
typedef enum {
  EOS_NETIF_TYPE_UNKNOWN = 0,
  EOS_NETIF_TYPE_STA,
  EOS_NETIF_TYPE_AP,
  EOS_NETIF_TYPE_BRIDGE,
  EOS_NETIF_TYPE_RADIO,
} eos_netif_type_t;

// Netif IPv4 address configuration (dotted quad strings, e.g. "192.168.1.100")
typedef struct {
  char ip[16];
  char netmask[16];
  char gw[16];
} eos_netif_ip_info_t;

// Netif general information query structure
typedef struct {
  char name[16];            // e.g. "sta0", "ap0", "wifi0", "br0"
  eos_netif_type_t type;    // Interface type
  uint32_t flags;           // EOS_NETIF_FLAG_*
  uint8_t mac[6];           // MAC address
  eos_netif_ip_info_t ip;   // Current IP configuration
  char dns_main[16];        // Primary DNS server
  char dns_backup[16];      // Secondary DNS server
  char hostname[32];        // Configured hostname
  int8_t rssi;              // Current signal strength (dBm, 0 if N/A)
  char extra[128];          // Associated SSID / status summary
} eos_netif_info_t;

// STA connection request
typedef struct {
  char ssid[33];            // Target SSID (null-terminated)
  char password[64];        // Passphrase (null-terminated, empty for open)
  uint8_t bssid[6];         // Optional target BSSID (all zeros to ignore)
  uint8_t channel;          // Optional target channel (0 for any)
  uint32_t timeout_ms;      // Connection timeout in milliseconds (0 for 10000ms)
} eos_sta_connect_t;

// STA connected AP status
typedef struct {
  char ssid[33];
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t authmode;         // 0: Open, 1: WEP, 2: WPA, 3: WPA2, 4: WPA/WPA2, 5: WPA3
  bool connected;
} eos_sta_ap_info_t;

// SoftAP configuration
typedef struct {
  char ssid[33];            // SoftAP SSID
  char password[64];        // Passphrase (empty for open)
  uint8_t channel;          // WiFi channel (1-13)
  uint8_t authmode;         // 0: Open, 2: WPA_PSK, 3: WPA2_PSK, 4: WPA/WPA2, 5: WPA3
  uint8_t max_connection;   // Max connected stations (1-10)
  bool ssid_hidden;         // Broadcast SSID or hidden
} eos_ap_config_t;

// Single connected station record on SoftAP
typedef struct {
  uint8_t mac[6];
  int8_t rssi;
} eos_ap_sta_record_t;

// Connected stations list on SoftAP
typedef struct {
  eos_ap_sta_record_t stas[10];
  uint8_t count;
} eos_ap_sta_list_t;

// WiFi Scan modes
typedef enum {
  EOS_WIFI_SCAN_ACTIVE = 0,
  EOS_WIFI_SCAN_PASSIVE = 1,
} eos_wifi_scan_mode_t;

// WiFi Scan request
typedef struct {
  eos_wifi_scan_mode_t mode; // Active or passive scan
  uint8_t channel;           // Channel (0 for all channels 1..13)
  bool show_hidden;          // Include hidden networks
  uint16_t dwell_time_ms;    // Dwell time per channel in ms (0 for default 120ms)
} eos_wifi_scan_config_t;

// Discovered Access Point record
typedef struct {
  char ssid[33];
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t authmode;
  bool is_hidden;
} eos_wifi_scan_ap_t;

// Scan results buffer container
typedef struct {
  eos_wifi_scan_ap_t *aps;  // Destination array allocated by caller
  size_t max_count;         // Maximum records aps can hold
  size_t count;             // Populated record count returned by driver
} eos_wifi_scan_results_t;

// Raw 802.11 packet structure for RX and TX via /dev/wifi0
typedef struct {
  int8_t rssi;              // Received signal strength in dBm
  uint8_t channel;          // Channel received on
  uint8_t rate;             // PHY data rate
  uint8_t flags;            // Frame flags
  uint16_t length;          // Length of frame payload bytes
  uint8_t payload[512];     // Raw 802.11 MAC frame (Frame Control, Addr1..4, body, etc.)
} eos_wifi_raw_packet_t;

// Promiscuous mode configuration
typedef struct {
  bool enable;              // Enable or disable promiscuous mode
  bool filter_mgmt;         // Capture 802.11 management frames (Beacons, Probe, Deauth)
  bool filter_ctrl;         // Capture 802.11 control frames (RTS, CTS, ACK)
  bool filter_data;         // Capture 802.11 data frames
  bool filter_misc;         // Capture other frames
} eos_wifi_promisc_cfg_t;

// Bridge status and port list
typedef struct {
  char name[16];            // Bridge interface name (e.g. "br0")
  char ports[4][16];        // Member port interface names (e.g. "sta0", "ap0")
  uint8_t port_count;       // Count of attached member ports
  bool napt_enabled;        // L3 Network Address Translation (NAPT) routing enabled
  char napt_uplink[16];     // Uplink interface for NAPT routing (e.g. "sta0")
} eos_netif_bridge_info_t;

#ifdef __cplusplus
}
#endif
