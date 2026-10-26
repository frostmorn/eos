#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ecore/dev.h"
#include "ecore/netif.h"
#include <esp_netif.h>

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Initialize core TCP/IP stack (NVS, esp_netif, default event loop)
bool eos_net_ensure_init(void);

// Initialize WiFi subsystem (WIFI_INIT_CONFIG_DEFAULT, RAM storage)
bool eos_net_wifi_ensure_init(void);

// Check if WiFi physical radio is running
bool eos_net_wifi_is_radio_on(void);

// Start/Stop WiFi physical radio transceiver
int eos_net_wifi_radio_start(void);
int eos_net_wifi_radio_stop(void);

// Recompute and apply WiFi mode (WIFI_MODE_STA, WIFI_MODE_AP, or WIFI_MODE_APSTA)
int eos_net_wifi_update_mode(void);

// Netif pointers
esp_netif_t *eos_net_get_sta_netif(void);
void eos_net_set_sta_netif(esp_netif_t *netif);

esp_netif_t *eos_net_get_ap_netif(void);
void eos_net_set_ap_netif(esp_netif_t *netif);

// Device pointers
eos_dev_t *eos_net_get_wifi_dev(void);
void eos_net_set_wifi_dev(eos_dev_t *dev);

eos_dev_t *eos_net_get_sta_dev(void);
void eos_net_set_sta_dev(eos_dev_t *dev);

eos_dev_t *eos_net_get_ap_dev(void);
void eos_net_set_ap_dev(eos_dev_t *dev);

// STA connection state
bool eos_net_sta_is_connected(void);
int eos_net_sta_wait_connect(uint32_t timeout_ms);
void eos_net_sta_get_connected_info(eos_sta_ap_info_t *out_info);
void eos_net_sta_set_connected_info(const eos_sta_ap_info_t *info);

// STA desired state
void eos_net_sta_set_enabled(bool enabled);
bool eos_net_sta_is_enabled(void);

// AP desired state
void eos_net_ap_set_enabled(bool enabled);
bool eos_net_ap_is_enabled(void);

// Bridge / NAPT routing between STA and AP
bool eos_net_is_napt_enabled(void);
int eos_net_set_napt_enabled(bool enable);

// External controller power management (e.g. ESP32-C6 on JC4880P443C)
void eos_net_wifi_set_chip_pu(int pin);
int eos_net_wifi_get_chip_pu(void);

#ifdef __cplusplus
}
#endif
