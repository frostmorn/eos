#include "includes.h"
#ifdef EOS_DRV_NET_WIFI_ENABLED

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/netif.h"
#include "emisc/netmisc.h"

#include <errno.h>
#include <esp_mac.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <string.h>
#include <unistd.h>

#if EOS_HAVE_WIFI
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

#define RAW_PACKET_QUEUE_SIZE 16
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define WIFI_SCAN_DONE_BIT    BIT2

typedef struct {
  eos_wifi_raw_packet_t packets[RAW_PACKET_QUEUE_SIZE];
  uint16_t head;
  uint16_t tail;
  uint16_t count;
  SemaphoreHandle_t mutex;
  SemaphoreHandle_t sem_rx;
  bool promisc_enabled;
  eos_wifi_promisc_cfg_t promisc_cfg;
  eos_wifi_scan_ap_t *scan_results;
  uint16_t scan_count;
  uint8_t channel;
  int8_t tx_power;
  char country[3];
  int chip_pu_pin;

  // Subsystem & stack state:
  bool net_inited;
  bool wifi_inited;
  bool radio_on;
  bool sta_enabled;
  bool ap_enabled;
  bool sta_connected;
  bool napt_enabled;
  EventGroupHandle_t wifi_event_group;
  eos_sta_ap_info_t sta_ap_info;
} wifi_radio_state_t;

static wifi_radio_state_t *s_radio_state = NULL;

#if EOS_HAVE_WIFI
static void IRAM_ATTR wifi_promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!s_radio_state || !s_radio_state->promisc_enabled) return;

  const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
  if (!pkt || pkt->rx_ctrl.sig_len == 0) return;

  wifi_radio_state_t *st = s_radio_state;

  BaseType_t high_task_wakeup = pdFALSE;
  if (xSemaphoreTakeFromISR(st->mutex, &high_task_wakeup) == pdTRUE) {
    if (st->count < RAW_PACKET_QUEUE_SIZE) {
      eos_wifi_raw_packet_t *item = &st->packets[st->head];
      item->rssi = (int8_t)pkt->rx_ctrl.rssi;
      item->channel = (uint8_t)pkt->rx_ctrl.channel;
      item->rate = (uint8_t)pkt->rx_ctrl.rate;
      item->flags = (uint8_t)type;

      uint16_t copy_len = pkt->rx_ctrl.sig_len;
      if (copy_len > sizeof(item->payload)) {
        copy_len = sizeof(item->payload);
      }
      item->length = copy_len;
      memcpy(item->payload, pkt->payload, copy_len);

      if (copy_len >= 2) {
        uint16_t fc = (uint16_t)item->payload[0] | ((uint16_t)item->payload[1] << 8);
        item->frame_type = (uint8_t)((fc >> 2) & 0x3);
        item->frame_subtype = (uint8_t)((fc >> 4) & 0xF);
        switch (item->frame_type) {
          case 0: memcpy(item->type_name, "MGMT", 5); break;
          case 1: memcpy(item->type_name, "CTRL", 5); break;
          case 2: memcpy(item->type_name, "DATA", 5); break;
          default: memcpy(item->type_name, "RESV", 5); break;
        }
      } else {
        item->frame_type = 3;
        item->frame_subtype = 0;
        memcpy(item->type_name, "RAW", 4);
      }

      if (copy_len >= 16) {
        memcpy(item->addr1, &item->payload[4], 6);
        memcpy(item->addr2, &item->payload[10], 6);
        item->has_addrs = true;
      } else {
        memset(item->addr1, 0, 6);
        memset(item->addr2, 0, 6);
        item->has_addrs = false;
      }

      if (copy_len >= 22) {
        memcpy(item->addr3, &item->payload[16], 6);
      } else {
        memset(item->addr3, 0, 6);
      }

      st->head = (st->head + 1) % RAW_PACKET_QUEUE_SIZE;
      st->count++;

      if (st->sem_rx) {
        xSemaphoreGiveFromISR(st->sem_rx, &high_task_wakeup);
      }
    }
    xSemaphoreGiveFromISR(st->mutex, &high_task_wakeup);
  }

  if (high_task_wakeup == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  wifi_radio_state_t *st = (wifi_radio_state_t *)arg;
  if (!st) st = s_radio_state;
  if (!st) return;

  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        EOS_LOGI("wifi: station interface started");
        break;
      case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        memset(&st->sta_ap_info, 0, sizeof(st->sta_ap_info));
        memcpy(st->sta_ap_info.ssid, event->ssid, event->ssid_len);
        st->sta_ap_info.ssid[event->ssid_len] = '\0';
        memcpy(st->sta_ap_info.bssid, event->bssid, 6);
        st->sta_ap_info.channel = event->channel;
        st->sta_ap_info.authmode = event->authmode;
        st->sta_ap_info.connected = true;
        EOS_LOGI("wifi: connected to AP \"%s\" (channel %d)", st->sta_ap_info.ssid, event->channel);
        break;
      }
      case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        st->sta_connected = false;
        st->sta_ap_info.connected = false;
        EOS_LOGW("wifi: disconnected from AP, reason: %d", event->reason);
        if (st->wifi_event_group) {
          xEventGroupSetBits(st->wifi_event_group, WIFI_FAIL_BIT);
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
      case WIFI_EVENT_SCAN_DONE: {
        wifi_event_sta_scan_done_t *scan_evt = (wifi_event_sta_scan_done_t *)event_data;
        EOS_LOGI("wifi: scan completed event received (status=%lu, ap_count=%u)",
                 scan_evt ? scan_evt->status : 0, scan_evt ? scan_evt->number : 0);
        if (st->wifi_event_group) {
          xEventGroupSetBits(st->wifi_event_group, WIFI_SCAN_DONE_BIT);
        }
        break;
      }
      default:
        break;
    }
  } else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      st->sta_connected = true;
      EOS_LOGI("wifi: got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
      if (st->wifi_event_group) {
        xEventGroupSetBits(st->wifi_event_group, WIFI_CONNECTED_BIT);
      }
#if defined(IP_NAPT) && IP_NAPT
      if (st->napt_enabled) {
        ip_napt_enable(event->ip_info.ip.addr, 1);
        EOS_LOGI("wifi: NAPT enabled on STA interface " IPSTR, IP2STR(&event->ip_info.ip));
      }
#endif
    }
  }
}
#endif

static bool wifi_ensure_net_init(wifi_radio_state_t *st) {
  if (st->net_inited) return true;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    EOS_LOGE("wifi: nvs_flash_init failed: %s", esp_err_to_name(ret));
  }

  ret = esp_netif_init();
  if (ret != ESP_OK) {
    EOS_LOGE("wifi: esp_netif_init failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    EOS_LOGE("wifi: esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
    return false;
  }

  if (!st->wifi_event_group) {
    st->wifi_event_group = xEventGroupCreate();
  }

#if EOS_HAVE_WIFI
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &wifi_event_handler, st, NULL);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &wifi_event_handler, st, NULL);
#endif

  st->net_inited = true;
  return true;
}

static bool wifi_ensure_wifi_init(wifi_radio_state_t *st) {
  if (st->wifi_inited) return true;
  if (!wifi_ensure_net_init(st)) return false;

#if EOS_HAVE_WIFI
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t ret = esp_wifi_init(&cfg);
  if (ret != ESP_OK) {
    EOS_LOGE("wifi: esp_wifi_init failed: %s", esp_err_to_name(ret));
    return false;
  }

  esp_wifi_set_storage(WIFI_STORAGE_RAM);
#if !defined(CONFIG_ESP_HOSTED_ENABLED) || !CONFIG_ESP_HOSTED_ENABLED
  if (st->tx_power > 0) {
    esp_wifi_set_max_tx_power(st->tx_power * 4);
  }
#endif
  st->wifi_inited = true;
  return true;
#else
  st->wifi_inited = true;
  return true;
#endif
}

static int wifi_radio_start(wifi_radio_state_t *st) {
#if !defined(CONFIG_ESP_HOSTED_ENABLED) || !CONFIG_ESP_HOSTED_ENABLED
  if (st->chip_pu_pin >= 0) {
    gpio_set_level((gpio_num_t)st->chip_pu_pin, 1);
    usleep(10 * 1000);
    EOS_LOGI("wifi: external controller (C6) powered ON (GPIO %d HIGH)", st->chip_pu_pin);
  }
#endif

#if EOS_HAVE_WIFI
  if (!wifi_ensure_wifi_init(st)) return -1;

  if (!st->radio_on) {
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STOPPED) {
      EOS_LOGE("wifi: esp_wifi_start failed: %s", esp_err_to_name(err));
      return -1;
    }
    st->radio_on = true;
#if !defined(CONFIG_ESP_HOSTED_ENABLED) || !CONFIG_ESP_HOSTED_ENABLED
    if (st->tx_power > 0) {
      esp_wifi_set_max_tx_power(st->tx_power * 4);
    }
#endif
    EOS_LOGI("wifi: radio transceiver started");
  }
  return 0;
#else
  st->radio_on = true;
  return 0;
#endif
}

static int wifi_radio_stop(wifi_radio_state_t *st) {
#if !defined(CONFIG_ESP_HOSTED_ENABLED) || !CONFIG_ESP_HOSTED_ENABLED
  if (st->chip_pu_pin >= 0) {
    gpio_set_level((gpio_num_t)st->chip_pu_pin, 0);
    EOS_LOGI("wifi: external controller (C6) powered OFF (GPIO %d LOW)", st->chip_pu_pin);
  }
#endif

#if EOS_HAVE_WIFI
  if (st->radio_on) {
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
      EOS_LOGW("wifi: esp_wifi_stop: %s", esp_err_to_name(err));
    }
    st->radio_on = false;
    st->sta_connected = false;
    EOS_LOGI("wifi: radio transceiver powered down");
  }
  return 0;
#else
  st->radio_on = false;
  return 0;
#endif
}

static int wifi_update_mode(wifi_radio_state_t *st) {
#if EOS_HAVE_WIFI
  if (!st->wifi_inited) return 0;

  wifi_mode_t target_mode = WIFI_MODE_NULL;
  if (st->sta_enabled && st->ap_enabled) {
    target_mode = WIFI_MODE_APSTA;
  } else if (st->sta_enabled) {
    target_mode = WIFI_MODE_STA;
  } else if (st->ap_enabled) {
    target_mode = WIFI_MODE_AP;
  }

  if (target_mode != WIFI_MODE_NULL) {
    esp_err_t err = esp_wifi_set_mode(target_mode);
    if (err != ESP_OK) {
      EOS_LOGE("wifi: esp_wifi_set_mode(%d) failed: %s", (int)target_mode, esp_err_to_name(err));
      return -1;
    }
    if (!st->radio_on) {
      wifi_radio_start(st);
    }
  } else {
    if (st->radio_on) {
      wifi_radio_stop(st);
    }
    esp_wifi_set_mode(WIFI_MODE_NULL);
  }
  return 0;
#else
  return 0;
#endif
}

static int wifi_set_napt_enabled(wifi_radio_state_t *st, bool enable) {
  st->napt_enabled = enable;
#if EOS_HAVE_WIFI && defined(IP_NAPT) && IP_NAPT
  if (st->sta_connected) {
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
      esp_netif_ip_info_t ip_info;
      if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
        if (enable) {
          ip_napt_enable(ip_info.ip.addr, 1);
          EOS_LOGI("wifi: NAPT enabled on STA interface " IPSTR, IP2STR(&ip_info.ip));
        } else {
          ip_napt_disable();
          EOS_LOGI("wifi: NAPT disabled");
        }
      }
    }
  }
#endif
  return 0;
}

bool driver_net_wifi_init(eos_dev_t *dev) {
  wifi_radio_state_t *st = calloc(1, sizeof(wifi_radio_state_t));
  if (!st) return false;

  st->mutex = xSemaphoreCreateMutex();
  st->sem_rx = xSemaphoreCreateBinary();
  st->channel = 1;
  st->tx_power = (int8_t)eos_cfg_get_i(dev->cfg, "tx_power", 14);
  memcpy(st->country, "US", 2);
  st->chip_pu_pin = -1;

  int chip_pu = eos_pin_get_no(dev->pins, "chip_pu");
  if (chip_pu < 0) chip_pu = eos_pin_get_no(dev->pins, "rst");
  if (chip_pu < 0 && dev->parent) {
    chip_pu = eos_pin_get_no(dev->parent->pins, "rst");
  }
  if (chip_pu >= 0) {
    st->chip_pu_pin = chip_pu;
#if !defined(CONFIG_ESP_HOSTED_ENABLED) || !CONFIG_ESP_HOSTED_ENABLED
    gpio_reset_pin((gpio_num_t)chip_pu);
    gpio_set_direction((gpio_num_t)chip_pu, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)chip_pu, 0); // Keep in reset until radio is started
    EOS_LOGI("wifi: external controller (C6) chip_pu pin %d configured (LOW / reset)", chip_pu);
#else
    EOS_LOGI("wifi: external controller reset pin %d managed by esp_hosted", chip_pu);
#endif
  }

  dev->state = st;
  s_radio_state = st;

  wifi_ensure_wifi_init(st);

  EOS_LOGI("wifi: radio device initialized (/dev/wifi0)");
  return true;
}

void driver_net_wifi_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  wifi_radio_state_t *st = (wifi_radio_state_t *)dev->state;

  if (st->chip_pu_pin >= 0) {
#if !defined(CONFIG_ESP_HOSTED_ENABLED) || !CONFIG_ESP_HOSTED_ENABLED
    gpio_set_level((gpio_num_t)st->chip_pu_pin, 0);
#endif
  }

#if EOS_HAVE_WIFI
  if (st->promisc_enabled) {
    esp_wifi_set_promiscuous(false);
  }
  wifi_radio_stop(st);
#endif

  if (st->wifi_event_group) {
    vEventGroupDelete(st->wifi_event_group);
    st->wifi_event_group = NULL;
  }
  if (st->scan_results) {
    free(st->scan_results);
  }
  if (st->mutex) vSemaphoreDelete(st->mutex);
  if (st->sem_rx) vSemaphoreDelete(st->sem_rx);

  free(st);
  dev->state = NULL;
  s_radio_state = NULL;
}

int driver_net_wifi_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)dev;
  (void)path;
  (void)flags;
  (void)mode;
  return 0;
}

int driver_net_wifi_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_net_wifi_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) {
    errno = EINVAL;
    return -1;
  }
  if (size < sizeof(eos_wifi_raw_packet_t)) {
    errno = EINVAL;
    return -1;
  }

  wifi_radio_state_t *st = (wifi_radio_state_t *)dev->state;
  if (!st->promisc_enabled) {
    errno = ENODATA;
    return -1;
  }

  // Wait for packet with 1 second timeout
  if (xSemaphoreTake(st->sem_rx, pdMS_TO_TICKS(1000)) != pdTRUE) {
    errno = ETIMEDOUT;
    return -1;
  }

  if (xSemaphoreTake(st->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    errno = EBUSY;
    return -1;
  }

  if (st->count == 0) {
    xSemaphoreGive(st->mutex);
    errno = EAGAIN;
    return -1;
  }

  eos_wifi_raw_packet_t *item = &st->packets[st->tail];
  memcpy(dst, item, sizeof(eos_wifi_raw_packet_t));
  st->tail = (st->tail + 1) % RAW_PACKET_QUEUE_SIZE;
  st->count--;

  xSemaphoreGive(st->mutex);
  return sizeof(eos_wifi_raw_packet_t);
}

ssize_t driver_net_wifi_write(eos_dev_t *dev, int fd, const void *src, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !src || size == 0) {
    errno = EINVAL;
    return -1;
  }

  wifi_radio_state_t *st = (wifi_radio_state_t *)dev->state;
  if (!st->radio_on) {
    errno = ENETDOWN;
    return -1;
  }

#if EOS_HAVE_WIFI
  esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, src, size, true);
  if (err != ESP_OK) {
    errno = EIO;
    return -1;
  }
  return (ssize_t)size;
#else
  return (ssize_t)size;
#endif
}

int driver_net_wifi_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) return -1;
  wifi_radio_state_t *st = (wifi_radio_state_t *)dev->state;

  switch (cmd) {
    case EOS_WIFI_IOCTL_ENSURE_INIT: {
      return wifi_ensure_wifi_init(st) ? 0 : -1;
    }
    case EOS_WIFI_IOCTL_SET_MODE: {
      int if_type = va_arg(args, int); // 0 = STA, 1 = AP
      bool enable = (bool)va_arg(args, int);
      if (if_type == 0) {
        st->sta_enabled = enable;
      } else if (if_type == 1) {
        st->ap_enabled = enable;
      }
      return wifi_update_mode(st);
    }
    case EOS_WIFI_IOCTL_IS_RADIO_ON: {
      bool *out_on = va_arg(args, bool *);
      if (!out_on) return -1;
      *out_on = st->radio_on;
      return 0;
    }
    case EOS_WIFI_IOCTL_IS_CONNECTED: {
      bool *out_conn = va_arg(args, bool *);
      if (!out_conn) return -1;
      *out_conn = st->sta_connected;
      return 0;
    }
    case EOS_WIFI_IOCTL_WAIT_CONNECT: {
      uint32_t timeout_ms = va_arg(args, uint32_t);
      if (!st->wifi_event_group) return -1;
      if (timeout_ms == 0) timeout_ms = 10000;
      xEventGroupClearBits(st->wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
      EventBits_t bits = xEventGroupWaitBits(st->wifi_event_group,
                                             WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                             pdFALSE, pdFALSE,
                                             pdMS_TO_TICKS(timeout_ms));
      return (bits & WIFI_CONNECTED_BIT) ? 0 : -1;
    }
    case EOS_WIFI_IOCTL_GET_CONNECTED_INFO: {
      eos_sta_ap_info_t *out_info = va_arg(args, eos_sta_ap_info_t *);
      if (!out_info) return -1;
      *out_info = st->sta_ap_info;
#if EOS_HAVE_WIFI
      if (st->sta_connected) {
        wifi_ap_record_t rec;
        if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
          out_info->rssi = rec.rssi;
          out_info->channel = rec.primary;
          out_info->authmode = rec.authmode;
        }
      }
#endif
      return 0;
    }
    case EOS_WIFI_IOCTL_SET_CONNECTED_INFO: {
      const eos_sta_ap_info_t *info = va_arg(args, const eos_sta_ap_info_t *);
      if (info) {
        st->sta_ap_info = *info;
        st->sta_connected = info->connected;
      } else {
        memset(&st->sta_ap_info, 0, sizeof(st->sta_ap_info));
        st->sta_connected = false;
      }
      return 0;
    }
    case EOS_WIFI_IOCTL_SET_NAPT: {
      bool enable = (bool)va_arg(args, int);
      return wifi_set_napt_enabled(st, enable);
    }
    case EOS_RADIO_IOCTL_POWER: {
      int state = va_arg(args, int);
      if (state == 1) {
        return wifi_radio_start(st);
      } else {
        return wifi_radio_stop(st);
      }
    }
    case EOS_RADIO_IOCTL_GET_POWER: {
      int *out_state = va_arg(args, int *);
      if (!out_state) return -1;
      *out_state = st->radio_on ? 1 : 0;
      return 0;
    }
    case EOS_RADIO_IOCTL_SET_CHANNEL: {
      int ch = va_arg(args, int);
      if (ch < 1 || ch > 14) return -1;
      st->channel = (uint8_t)ch;
#if EOS_HAVE_WIFI
      if (st->radio_on) {
        esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
      }
#endif
      return 0;
    }
    case EOS_RADIO_IOCTL_GET_CHANNEL: {
      uint8_t *out_ch = va_arg(args, uint8_t *);
      if (!out_ch) return -1;
#if EOS_HAVE_WIFI
      if (st->radio_on) {
        uint8_t ch = 0;
        wifi_second_chan_t second;
        if (esp_wifi_get_channel(&ch, &second) == ESP_OK && ch != 0) {
          st->channel = ch;
        }
      }
#endif
      *out_ch = st->channel;
      return 0;
    }
    case EOS_RADIO_IOCTL_SET_TX_POWER: {
      int power = va_arg(args, int);
      st->tx_power = (int8_t)power;
#if EOS_HAVE_WIFI
      int8_t max_p = (int8_t)(power * 4);
      esp_wifi_set_max_tx_power(max_p);
#endif
      return 0;
    }
    case EOS_RADIO_IOCTL_GET_TX_POWER: {
      int8_t *out_power = va_arg(args, int8_t *);
      if (!out_power) return -1;
#if EOS_HAVE_WIFI
      int8_t max_p = 0;
      if (esp_wifi_get_max_tx_power(&max_p) == ESP_OK) {
        st->tx_power = max_p / 4;
      }
#endif
      *out_power = st->tx_power;
      return 0;
    }
    case EOS_RADIO_IOCTL_SET_COUNTRY: {
      const char *cc = va_arg(args, const char *);
      if (!cc) return -1;
      strncpy(st->country, cc, 2);
      st->country[2] = '\0';
#if EOS_HAVE_WIFI
      wifi_country_t country = {0};
      strncpy(country.cc, st->country, 3);
      country.schan = 1;
      country.nchan = 13;
      country.policy = WIFI_COUNTRY_POLICY_MANUAL;
      esp_wifi_set_country(&country);
#endif
      return 0;
    }
    case EOS_RADIO_IOCTL_GET_COUNTRY: {
      char *out_cc = va_arg(args, char *);
      if (!out_cc) return -1;
      strncpy(out_cc, st->country, 2);
      out_cc[2] = '\0';
      return 0;
    }
    case EOS_RADIO_IOCTL_SCAN_START: {
      const eos_wifi_scan_config_t *scan_cfg = va_arg(args, const eos_wifi_scan_config_t *);
#if EOS_HAVE_WIFI
      if (!wifi_ensure_wifi_init(st)) return -1;

      wifi_mode_t cur_mode = WIFI_MODE_NULL;
      esp_wifi_get_mode(&cur_mode);
      if (cur_mode != WIFI_MODE_STA && cur_mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode((cur_mode == WIFI_MODE_AP) ? WIFI_MODE_APSTA : WIFI_MODE_STA);
      }

      if (!st->radio_on) {
        wifi_radio_start(st);
      }

      wifi_scan_config_t *p_scfg = NULL;
      wifi_scan_config_t scfg = {0};
      if (scan_cfg && (scan_cfg->channel > 0 || scan_cfg->mode != 0)) {
        scfg.show_hidden = scan_cfg->show_hidden;
        scfg.channel = scan_cfg->channel;
        scfg.scan_type = (scan_cfg->mode == EOS_WIFI_SCAN_PASSIVE)
                             ? WIFI_SCAN_TYPE_PASSIVE
                             : WIFI_SCAN_TYPE_ACTIVE;
        uint16_t dwell = (scan_cfg->dwell_time_ms > 0) ? scan_cfg->dwell_time_ms : 120;
        if (scfg.scan_type == WIFI_SCAN_TYPE_PASSIVE) {
          scfg.scan_time.passive = dwell;
        } else {
          scfg.scan_time.active.min = dwell / 2;
          scfg.scan_time.active.max = dwell;
        }
        p_scfg = &scfg;
      }

      EOS_LOGI("wifi: starting WiFi scan...");

      EventGroupHandle_t evg = st->wifi_event_group;
      if (evg) {
        xEventGroupClearBits(evg, WIFI_SCAN_DONE_BIT);
      }

      esp_err_t err = esp_wifi_scan_start(p_scfg, false);
      if (err != ESP_OK) {
        EOS_LOGW("wifi: async scan start failed (%s), trying blocking scan...", esp_err_to_name(err));
        err = esp_wifi_scan_start(p_scfg, true);
        if (err != ESP_OK) {
          EOS_LOGE("wifi: scan start failed: %s", esp_err_to_name(err));
          return -1;
        }
      } else if (evg) {
        EOS_LOGI("wifi: waiting for scan completion...");
        xEventGroupWaitBits(evg, WIFI_SCAN_DONE_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
      }

      uint16_t ap_count = 0;
      esp_err_t num_err = esp_wifi_scan_get_ap_num(&ap_count);
      EOS_LOGI("wifi: esp_wifi_scan_get_ap_num returned %s, ap_count=%u",
               esp_err_to_name(num_err), (unsigned)ap_count);

      if (st->scan_results) {
        free(st->scan_results);
        st->scan_results = NULL;
      }
      st->scan_count = 0;

      if (ap_count > 0) {
        wifi_ap_record_t *records = malloc(ap_count * sizeof(wifi_ap_record_t));
        if (records) {
          uint16_t fetched = ap_count;
          esp_err_t rec_err = esp_wifi_scan_get_ap_records(&fetched, records);
          if (rec_err == ESP_OK) {
            st->scan_results = malloc(fetched * sizeof(eos_wifi_scan_ap_t));
            if (st->scan_results) {
              st->scan_count = fetched;
              for (size_t i = 0; i < fetched; i++) {
                strncpy(st->scan_results[i].ssid, (const char *)records[i].ssid, 32);
                st->scan_results[i].ssid[32] = '\0';
                memcpy(st->scan_results[i].bssid, records[i].bssid, 6);
                st->scan_results[i].rssi = records[i].rssi;
                st->scan_results[i].channel = records[i].primary;
                st->scan_results[i].authmode = records[i].authmode;
                st->scan_results[i].is_hidden = (records[i].ssid[0] == '\0');
                strncpy(st->scan_results[i].auth_name,
                        eos_net_wifi_auth_name(st->scan_results[i].authmode),
                        sizeof(st->scan_results[i].auth_name) - 1);
              }
            }
          } else {
            EOS_LOGE("wifi: esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(rec_err));
          }
          free(records);
        }
      }
      EOS_LOGI("wifi: scan complete, %u APs found", (unsigned)st->scan_count);
      return 0;
#else
      if (!st->radio_on) {
        wifi_radio_start(st);
      }
      if (st->scan_results) {
        free(st->scan_results);
        st->scan_results = NULL;
      }
      st->scan_count = 0;
      EOS_LOGI("wifi: scan complete, %u APs found", (unsigned)st->scan_count);
      return 0;
#endif
    }
    case EOS_RADIO_IOCTL_SCAN_GET_COUNT: {
      uint16_t *out_count = va_arg(args, uint16_t *);
      if (!out_count) return -1;
      *out_count = st->scan_count;
      return 0;
    }
    case EOS_RADIO_IOCTL_SCAN_GET_RESULTS: {
      eos_wifi_scan_results_t *res = va_arg(args, eos_wifi_scan_results_t *);
      if (!res || !res->aps || res->max_count == 0) return -1;

      size_t to_copy = (st->scan_count < res->max_count) ? st->scan_count : res->max_count;
      if (to_copy > 0 && st->scan_results) {
        memcpy(res->aps, st->scan_results, to_copy * sizeof(eos_wifi_scan_ap_t));
      }
      res->count = to_copy;
      return 0;
    }
    case EOS_RADIO_IOCTL_SET_PROMISC: {
      const eos_wifi_promisc_cfg_t *cfg = va_arg(args, const eos_wifi_promisc_cfg_t *);
      if (!cfg) return -1;
      st->promisc_cfg = *cfg;
      st->promisc_enabled = cfg->enable;

#if EOS_HAVE_WIFI
      if (!st->radio_on && cfg->enable) {
        wifi_radio_start(st);
      }

      wifi_promiscuous_filter_t filt = {0};
      if (cfg->filter_mgmt) filt.filter_mask |= WIFI_PROMIS_FILTER_MASK_MGMT;
      if (cfg->filter_ctrl) filt.filter_mask |= WIFI_PROMIS_FILTER_MASK_CTRL;
      if (cfg->filter_data) filt.filter_mask |= WIFI_PROMIS_FILTER_MASK_DATA;
      if (cfg->filter_misc) filt.filter_mask |= WIFI_PROMIS_FILTER_MASK_MISC;
      if (filt.filter_mask == 0) filt.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;

      esp_wifi_set_promiscuous_filter(&filt);
      esp_wifi_set_promiscuous_rx_cb(cfg->enable ? wifi_promisc_rx_cb : NULL);
      esp_wifi_set_promiscuous(cfg->enable);
      EOS_LOGI("wifi: promiscuous mode %s", cfg->enable ? "enabled" : "disabled");
#endif
      return 0;
    }
    case EOS_RADIO_IOCTL_GET_PROMISC: {
      eos_wifi_promisc_cfg_t *out_cfg = va_arg(args, eos_wifi_promisc_cfg_t *);
      if (!out_cfg) return -1;
      *out_cfg = st->promisc_cfg;
      out_cfg->enable = st->promisc_enabled;
      return 0;
    }
    case EOS_NETIF_IOCTL_SET_UP: {
      return wifi_radio_start(st);
    }
    case EOS_NETIF_IOCTL_SET_DOWN: {
      return wifi_radio_stop(st);
    }
    case EOS_NETIF_IOCTL_GET_INFO: {
      eos_netif_info_t *info = va_arg(args, eos_netif_info_t *);
      if (!info) return -1;
      memset(info, 0, sizeof(*info));
      strncpy(info->name, "wifi0", sizeof(info->name) - 1);
      info->type = EOS_NETIF_TYPE_RADIO;
      if (st->radio_on) info->flags |= EOS_NETIF_FLAG_UP | EOS_NETIF_FLAG_LINK_UP;
      if (st->promisc_enabled) info->flags |= EOS_NETIF_FLAG_PROMISC;
      if (st->chip_pu_pin >= 0) {
        snprintf(info->extra, sizeof(info->extra), "ext C6 (PU GPIO %d), ch %d, %d dBm",
                 st->chip_pu_pin, st->channel, st->tx_power);
      } else {
        snprintf(info->extra, sizeof(info->extra), "internal PHY, ch %d, %d dBm, cc %s",
                 st->channel, st->tx_power, st->country);
      }
      return 0;
    }
    case EOS_NETIF_IOCTL_GET_MAC: {
      uint8_t *out_mac = va_arg(args, uint8_t *);
      if (!out_mac) return -1;
#if EOS_HAVE_WIFI
      if (esp_wifi_get_mac(WIFI_IF_STA, out_mac) == ESP_OK) {
        if (out_mac[0] || out_mac[1] || out_mac[2] || out_mac[3] || out_mac[4] || out_mac[5]) {
          return 0;
        }
      }
      if (esp_read_mac(out_mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        return 0;
      }
#endif
      if (esp_read_mac(out_mac, ESP_MAC_BASE) == ESP_OK) {
        return 0;
      }
      return -1;
    }
    case EOS_NETIF_IOCTL_SET_MAC: {
      const uint8_t *mac = va_arg(args, const uint8_t *);
      if (!mac) return -1;
#if EOS_HAVE_WIFI
      return esp_wifi_set_mac(WIFI_IF_STA, (uint8_t *)mac) == ESP_OK ? 0 : -1;
#else
      return -1;
#endif
    }
    default:
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_net_wifi = {
    EOS_DRV_INIT,
    .scope = "net",
    .name = "wifi",
    .devname = "wifi",
    .init = driver_net_wifi_init,
    .shutdown = driver_net_wifi_shutdown,
    .open = driver_net_wifi_open,
    .close = driver_net_wifi_close,
    .read = driver_net_wifi_read,
    .write = driver_net_wifi_write,
    .ioctl = driver_net_wifi_ioctl,
};

#endif
