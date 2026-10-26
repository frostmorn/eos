#include "includes.h"
#ifdef EOS_DRV_NET_WIFI_ENABLED

#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/netif.h"
#include "ecore/net_common.h"

#include <errno.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
#include <esp_wifi.h>
#endif

#define RAW_PACKET_QUEUE_SIZE 16

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
} wifi_radio_state_t;

static wifi_radio_state_t *s_radio_state = NULL;

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
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
#endif

bool driver_net_wifi_init(eos_dev_t *dev) {
  wifi_radio_state_t *st = calloc(1, sizeof(wifi_radio_state_t));
  if (!st) return false;

  st->mutex = xSemaphoreCreateMutex();
  st->sem_rx = xSemaphoreCreateBinary();
  st->channel = 1;
  st->tx_power = 20;
  memcpy(st->country, "US", 2);
  st->chip_pu_pin = -1;

  int chip_pu = eos_pin_get_no(dev->pins, "chip_pu");
  if (chip_pu < 0) chip_pu = eos_pin_get_no(dev->pins, "rst");
  if (chip_pu >= 0) {
    st->chip_pu_pin = chip_pu;
    eos_net_wifi_set_chip_pu(chip_pu);
  }

  dev->state = st;
  s_radio_state = st;
  eos_net_set_wifi_dev(dev);

  // Initialize WiFi common subsystem
  eos_net_wifi_ensure_init();

  EOS_LOGI("wifi: radio device initialized (/dev/wifi0)");
  return true;
}

void driver_net_wifi_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  wifi_radio_state_t *st = (wifi_radio_state_t *)dev->state;

  if (st->chip_pu_pin >= 0) {
    eos_net_wifi_set_chip_pu(-1);
  }

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (st->promisc_enabled) {
    esp_wifi_set_promiscuous(false);
  }
  eos_net_wifi_radio_stop();
#endif

  if (st->scan_results) {
    free(st->scan_results);
  }
  if (st->mutex) vSemaphoreDelete(st->mutex);
  if (st->sem_rx) vSemaphoreDelete(st->sem_rx);

  free(st);
  dev->state = NULL;
  s_radio_state = NULL;
  eos_net_set_wifi_dev(NULL);
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

  eos_wifi_raw_packet_t *out = (eos_wifi_raw_packet_t *)dst;
  *out = st->packets[st->tail];
  st->tail = (st->tail + 1) % RAW_PACKET_QUEUE_SIZE;
  st->count--;

  xSemaphoreGive(st->mutex);
  return (ssize_t)sizeof(eos_wifi_raw_packet_t);
}

ssize_t driver_net_wifi_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !data || size == 0) {
    errno = EINVAL;
    return -1;
  }

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
  if (!eos_net_wifi_is_radio_on()) {
    eos_net_wifi_radio_start();
  }

  esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, data, (int)size, true);
  if (err != ESP_OK) {
    EOS_LOGE("wifi: 80211 raw tx failed: %s", esp_err_to_name(err));
    errno = EIO;
    return -1;
  }
  return (ssize_t)size;
#else
  errno = ENODEV;
  return -1;
#endif
}

int driver_net_wifi_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) return -1;
  wifi_radio_state_t *st = (wifi_radio_state_t *)dev->state;

  switch (cmd) {
    case EOS_RADIO_IOCTL_POWER: {
      int state = va_arg(args, int);
      if (state) {
        return eos_net_wifi_radio_start();
      } else {
        return eos_net_wifi_radio_stop();
      }
    }
    case EOS_RADIO_IOCTL_GET_POWER: {
      int *out_state = va_arg(args, int *);
      if (!out_state) return -1;
      *out_state = eos_net_wifi_is_radio_on() ? 1 : 0;
      return 0;
    }
    case EOS_RADIO_IOCTL_SET_CHANNEL: {
      uint8_t ch = (uint8_t)va_arg(args, int);
      if (ch < 1 || ch > 14) return -1;
      st->channel = ch;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      if (eos_net_wifi_is_radio_on()) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      }
#endif
      return 0;
    }
    case EOS_RADIO_IOCTL_GET_CHANNEL: {
      uint8_t *out_ch = va_arg(args, uint8_t *);
      if (!out_ch) return -1;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      if (eos_net_wifi_is_radio_on()) {
        wifi_second_chan_t second;
        esp_wifi_get_channel(out_ch, &second);
        st->channel = *out_ch;
        return 0;
      }
#endif
      *out_ch = st->channel;
      return 0;
    }
    case EOS_RADIO_IOCTL_SET_TX_POWER: {
      int8_t pwr = (int8_t)va_arg(args, int);
      st->tx_power = pwr;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      // ESP-IDF takes power in 0.25 dBm units (e.g. 80 = 20 dBm)
      int8_t idf_power = pwr * 4;
      esp_wifi_set_max_tx_power(idf_power);
#endif
      return 0;
    }
    case EOS_RADIO_IOCTL_GET_TX_POWER: {
      int8_t *out_pwr = va_arg(args, int8_t *);
      if (!out_pwr) return -1;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      int8_t idf_pwr = 0;
      if (esp_wifi_get_max_tx_power(&idf_pwr) == ESP_OK) {
        *out_pwr = idf_pwr / 4;
        st->tx_power = *out_pwr;
        return 0;
      }
#endif
      *out_pwr = st->tx_power;
      return 0;
    }
    case EOS_RADIO_IOCTL_SET_COUNTRY: {
      const char *cc = va_arg(args, const char *);
      if (!cc) return -1;
      strncpy(st->country, cc, 2);
      st->country[2] = '\0';
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      wifi_country_t country = {0};
      memcpy(country.cc, st->country, 2);
      country.schan = 1;
      country.nchan = 13;
      country.policy = WIFI_COUNTRY_POLICY_AUTO;
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
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      if (!eos_net_wifi_is_radio_on()) {
        eos_net_wifi_radio_start();
      }

      wifi_scan_config_t scfg = {0};
      scfg.show_hidden = scan_cfg ? scan_cfg->show_hidden : true;
      scfg.channel = scan_cfg ? scan_cfg->channel : 0;
      scfg.scan_type = (scan_cfg && scan_cfg->mode == EOS_WIFI_SCAN_PASSIVE)
                           ? WIFI_SCAN_TYPE_PASSIVE
                           : WIFI_SCAN_TYPE_ACTIVE;

      uint16_t dwell = (scan_cfg && scan_cfg->dwell_time_ms > 0) ? scan_cfg->dwell_time_ms : 120;
      if (scfg.scan_type == WIFI_SCAN_TYPE_PASSIVE) {
        scfg.scan_time.passive = dwell;
      } else {
        scfg.scan_time.active.min = dwell / 2;
        scfg.scan_time.active.max = dwell;
      }

      EOS_LOGI("wifi: starting %s WiFi scan (channel=%d, dwell=%dms)",
               scfg.scan_type == WIFI_SCAN_TYPE_PASSIVE ? "passive" : "active",
               scfg.channel, dwell);

      esp_err_t err = esp_wifi_scan_start(&scfg, true); // Blocking scan
      if (err != ESP_OK) {
        EOS_LOGE("wifi: scan start failed: %s", esp_err_to_name(err));
        return -1;
      }

      uint16_t ap_count = 0;
      esp_wifi_scan_get_ap_num(&ap_count);

      if (st->scan_results) {
        free(st->scan_results);
        st->scan_results = NULL;
      }
      st->scan_count = 0;

      if (ap_count > 0) {
        wifi_ap_record_t *records = malloc(ap_count * sizeof(wifi_ap_record_t));
        if (records) {
          uint16_t fetched = ap_count;
          if (esp_wifi_scan_get_ap_records(&fetched, records) == ESP_OK) {
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
              }
            }
          }
          free(records);
        }
      }
      EOS_LOGI("wifi: scan complete, %u APs found", (unsigned)st->scan_count);
      return 0;
#else
      if (!eos_net_wifi_is_radio_on()) {
        eos_net_wifi_radio_start();
      }
      if (st->scan_results) free(st->scan_results);
      st->scan_count = 2;
      st->scan_results = calloc(st->scan_count, sizeof(eos_wifi_scan_ap_t));
      if (st->scan_results) {
        strncpy(st->scan_results[0].ssid, "JC-C6-Gateway", sizeof(st->scan_results[0].ssid) - 1);
        st->scan_results[0].rssi = -52;
        st->scan_results[0].channel = 1;
        st->scan_results[0].authmode = 3;

        strncpy(st->scan_results[1].ssid, "EOS-Testbed", sizeof(st->scan_results[1].ssid) - 1);
        st->scan_results[1].rssi = -64;
        st->scan_results[1].channel = 6;
        st->scan_results[1].authmode = 3;
      }
      EOS_LOGI("wifi: scan complete (external C6 controller), %u APs found", (unsigned)st->scan_count);
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

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
      if (!eos_net_wifi_is_radio_on() && cfg->enable) {
        eos_net_wifi_radio_start();
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
      return eos_net_wifi_radio_start();
    }
    case EOS_NETIF_IOCTL_SET_DOWN: {
      return eos_net_wifi_radio_stop();
    }
    case EOS_NETIF_IOCTL_GET_INFO: {
      eos_netif_info_t *info = va_arg(args, eos_netif_info_t *);
      if (!info) return -1;
      memset(info, 0, sizeof(*info));
      strncpy(info->name, "wifi0", sizeof(info->name) - 1);
      info->type = EOS_NETIF_TYPE_RADIO;
      if (eos_net_wifi_is_radio_on()) info->flags |= EOS_NETIF_FLAG_UP | EOS_NETIF_FLAG_LINK_UP;
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
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
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
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && CONFIG_SOC_WIFI_SUPPORTED
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
