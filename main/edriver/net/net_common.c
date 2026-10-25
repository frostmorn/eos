#include "net_common.h"
#include "ecore/log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <driver/gpio.h>
#include <string.h>

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#if defined(__has_include)
#if __has_include(<lwip/lwip_napt.h>)
#include <lwip/lwip_napt.h>
#elif __has_include(<lwip/ip4_napt.h>)
#include <lwip/ip4_napt.h>
#endif
#endif
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static bool s_net_inited = false;
static bool s_wifi_inited = false;
static bool s_radio_on = false;
static bool s_sta_enabled = false;
static bool s_ap_enabled = false;
static bool s_sta_connected = false;
static bool s_napt_enabled = false;
static int s_chip_pu_pin = -1;

void eos_net_wifi_set_chip_pu(int pin) {
  s_chip_pu_pin = pin;
  if (pin >= 0) {
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, 0); // Radio is OFF by default: keep external controller in reset
    EOS_LOGI("wifi: external controller (C6) chip_pu pin %d configured (LOW / reset)", pin);
  }
}

int eos_net_wifi_get_chip_pu(void) {
  return s_chip_pu_pin;
}

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static eos_dev_t *s_wifi_dev = NULL;
static eos_dev_t *s_sta_dev = NULL;
static eos_dev_t *s_ap_dev = NULL;

static EventGroupHandle_t s_wifi_event_group = NULL;
static eos_sta_ap_info_t s_sta_ap_info = {0};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        EOS_LOGI("wifi: station interface started");
        break;
      case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        memset(&s_sta_ap_info, 0, sizeof(s_sta_ap_info));
        memcpy(s_sta_ap_info.ssid, event->ssid, event->ssid_len);
        s_sta_ap_info.ssid[event->ssid_len] = '\0';
        memcpy(s_sta_ap_info.bssid, event->bssid, 6);
        s_sta_ap_info.channel = event->channel;
        s_sta_ap_info.authmode = event->authmode;
        s_sta_ap_info.connected = true;
        EOS_LOGI("wifi: connected to AP \"%s\" (channel %d)", s_sta_ap_info.ssid, event->channel);
        break;
      }
      case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        s_sta_connected = false;
        s_sta_ap_info.connected = false;
        EOS_LOGW("wifi: disconnected from AP, reason: %d", event->reason);
        if (s_wifi_event_group) {
          xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        break;
      }
      case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        EOS_LOGI("wifi: station %02x:%02x:%02x:%02x:%02x:%02x joined softAP (AID=%d)",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
        break;
      }
      case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        EOS_LOGI("wifi: station %02x:%02x:%02x:%02x:%02x:%02x left softAP (AID=%d)",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5], event->aid);
        break;
      }
      default:
        break;
    }
  } else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      s_sta_connected = true;
      EOS_LOGI("wifi: got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
      if (s_wifi_event_group) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
      }
      // If NAPT was requested, enable it on new STA IP
      if (s_napt_enabled) {
        eos_net_set_napt_enabled(true);
      }
    }
  }
#endif
}

bool eos_net_ensure_init(void) {
  if (s_net_inited) return true;

  // 1. Initialize NVS (required by WiFi subsystem)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    EOS_LOGE("net_common: nvs_flash_init failed: %s", esp_err_to_name(ret));
  }

  // 2. Initialize TCP/IP stack
  ret = esp_netif_init();
  if (ret != ESP_OK) {
    EOS_LOGE("net_common: esp_netif_init failed: %s", esp_err_to_name(ret));
    return false;
  }

  // 3. Initialize default system event loop
  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    EOS_LOGE("net_common: esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
    return false;
  }

  // 4. Create event group for async connection synchronization
  if (!s_wifi_event_group) {
    s_wifi_event_group = xEventGroupCreate();
  }

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  // 5. Register event handlers
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &wifi_event_handler, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &wifi_event_handler, NULL, NULL);
#endif

  s_net_inited = true;
  return true;
}

bool eos_net_wifi_ensure_init(void) {
  if (s_wifi_inited) return true;

  if (!eos_net_ensure_init()) return false;

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t ret = esp_wifi_init(&cfg);
  if (ret != ESP_OK) {
    EOS_LOGE("net_common: esp_wifi_init failed: %s", esp_err_to_name(ret));
    return false;
  }

  // Use RAM storage to avoid wear on SPI flash
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  s_wifi_inited = true;
  return true;
#else
  s_wifi_inited = true;
  return true;
#endif
}

bool eos_net_wifi_is_radio_on(void) {
  return s_radio_on;
}

int eos_net_wifi_radio_start(void) {
  if (s_chip_pu_pin >= 0) {
    gpio_set_level((gpio_num_t)s_chip_pu_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    EOS_LOGI("wifi: external controller (C6) powered ON (GPIO %d HIGH)", s_chip_pu_pin);
  }

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (!eos_net_wifi_ensure_init()) return -1;

  if (!s_radio_on) {
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
      EOS_LOGE("net_common: esp_wifi_start failed: %s", esp_err_to_name(err));
      return -1;
    }
    s_radio_on = true;
    EOS_LOGI("wifi: radio transceiver started");
  }
  return 0;
#else
  s_radio_on = true;
  return 0;
#endif
}

int eos_net_wifi_radio_stop(void) {
  if (s_chip_pu_pin >= 0) {
    gpio_set_level((gpio_num_t)s_chip_pu_pin, 0);
    EOS_LOGI("wifi: external controller (C6) powered OFF (GPIO %d LOW)", s_chip_pu_pin);
  }

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (s_radio_on) {
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
      EOS_LOGW("net_common: esp_wifi_stop: %s", esp_err_to_name(err));
    }
    s_radio_on = false;
    s_sta_connected = false;
    EOS_LOGI("wifi: radio transceiver powered down");
  }
  return 0;
#else
  s_radio_on = false;
  return 0;
#endif
}

int eos_net_wifi_update_mode(void) {
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (!s_wifi_inited) return 0;

  wifi_mode_t target_mode = WIFI_MODE_NULL;
  if (s_sta_enabled && s_ap_enabled) {
    target_mode = WIFI_MODE_APSTA;
  } else if (s_sta_enabled) {
    target_mode = WIFI_MODE_STA;
  } else if (s_ap_enabled) {
    target_mode = WIFI_MODE_AP;
  }

  if (target_mode != WIFI_MODE_NULL) {
    esp_err_t err = esp_wifi_set_mode(target_mode);
    if (err != ESP_OK) {
      EOS_LOGE("net_common: esp_wifi_set_mode(%d) failed: %s", (int)target_mode, esp_err_to_name(err));
      return -1;
    }
    if (!s_radio_on) {
      eos_net_wifi_radio_start();
    }
  } else {
    // Both interfaces disabled
    if (s_radio_on) {
      eos_net_wifi_radio_stop();
    }
    esp_wifi_set_mode(WIFI_MODE_NULL);
  }
  return 0;
#else
  return 0;
#endif
}

esp_netif_t *eos_net_get_sta_netif(void) { return s_sta_netif; }
void eos_net_set_sta_netif(esp_netif_t *netif) { s_sta_netif = netif; }

esp_netif_t *eos_net_get_ap_netif(void) { return s_ap_netif; }
void eos_net_set_ap_netif(esp_netif_t *netif) { s_ap_netif = netif; }

eos_dev_t *eos_net_get_wifi_dev(void) { return s_wifi_dev; }
void eos_net_set_wifi_dev(eos_dev_t *dev) { s_wifi_dev = dev; }

eos_dev_t *eos_net_get_sta_dev(void) { return s_sta_dev; }
void eos_net_set_sta_dev(eos_dev_t *dev) { s_sta_dev = dev; }

eos_dev_t *eos_net_get_ap_dev(void) { return s_ap_dev; }
void eos_net_set_ap_dev(eos_dev_t *dev) { s_ap_dev = dev; }

bool eos_net_sta_is_connected(void) {
  return s_sta_connected;
}

int eos_net_sta_wait_connect(uint32_t timeout_ms) {
  if (!s_wifi_event_group) return -1;
  if (timeout_ms == 0) timeout_ms = 10000;

  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE,
                                         pdMS_TO_TICKS(timeout_ms));
  if (bits & WIFI_CONNECTED_BIT) {
    return 0; // Success
  }
  return -1; // Timeout or failed
}

void eos_net_sta_get_connected_info(eos_sta_ap_info_t *out_info) {
  if (!out_info) return;
  *out_info = s_sta_ap_info;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (s_sta_connected) {
    wifi_ap_record_t rec;
    if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
      out_info->rssi = rec.rssi;
      out_info->channel = rec.primary;
      out_info->authmode = rec.authmode;
    }
  }
#endif
}

void eos_net_sta_set_connected_info(const eos_sta_ap_info_t *info) {
  if (info) {
    s_sta_ap_info = *info;
    s_sta_connected = info->connected;
  } else {
    memset(&s_sta_ap_info, 0, sizeof(s_sta_ap_info));
    s_sta_connected = false;
  }
}

void eos_net_sta_set_enabled(bool enabled) {
  s_sta_enabled = enabled;
}

bool eos_net_sta_is_enabled(void) {
  return s_sta_enabled;
}

void eos_net_ap_set_enabled(bool enabled) {
  s_ap_enabled = enabled;
}

bool eos_net_ap_is_enabled(void) {
  return s_ap_enabled;
}

bool eos_net_is_napt_enabled(void) {
  return s_napt_enabled;
}

int eos_net_set_napt_enabled(bool enable) {
  s_napt_enabled = enable;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED && defined(IP_NAPT) && IP_NAPT
  if (s_sta_netif) {
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
      if (enable) {
        ip_napt_enable(ip_info.ip.addr, 1);
        EOS_LOGI("net_common: NAPT enabled on STA interface " IPSTR, IP2STR(&ip_info.ip));
      } else {
        ip_napt_disable();
        EOS_LOGI("net_common: NAPT disabled");
      }
      return 0;
    }
  }
#endif
  return 0;
}
