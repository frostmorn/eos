#include "capsmgr.h"
#include "ecore/driver.h"
#include "emisc/fancymacro.h"
#include "emisc/kvec.h"
#include "pregen.h"
#include <driver/gpio.h>
#include <esp_bit_defs.h>
#include <soc/soc_caps.h>
#if __has_include(<soc/usb_pins.h>)
#include <soc/usb_pins.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool esp_gpio_is_reserved(uint64_t bitmask);

static kvec_psram_t(eos_cap_t) ecaps;
static SemaphoreHandle_t s_caps_lock = NULL;
static bool s_caps_initialized = false;

static inline void caps_lock(void) {
  if (s_caps_lock) {
    xSemaphoreTakeRecursive(s_caps_lock, portMAX_DELAY);
  }
}

static inline void caps_unlock(void) {
  if (s_caps_lock) {
    xSemaphoreGiveRecursive(s_caps_lock);
  }
}

static const char *s_cap_type_strings[EOS_CAPS_COUNT] = {
    [EOS_CAPS_GPIO] = "GPIO",
    [EOS_CAPS_LEDC_TIMER] = "LEDC_TIMER",
    [EOS_CAPS_LEDC_CHANNEL] = "LEDC_CH",
    [EOS_CAPS_GDMA_CHANNEL] = "GDMA_CH",
    [EOS_CAPS_SPI] = "SPI",
    [EOS_CAPS_I2C] = "I2C",
    [EOS_CAPS_UART] = "UART",
    [EOS_CAPS_RMT] = "RMT",
    [EOS_CAPS_GPTIMER] = "GPTIMER",
    [EOS_CAPS_I2S] = "I2S",
    [EOS_CAPS_INTR] = "INTR",
};

const char *eos_cap_type_to_str(eos_cap_type_t type) {
  if ((int)type < 0 || type >= EOS_CAPS_COUNT || !s_cap_type_strings[type]) {
    return "UNKNOWN";
  }
  return s_cap_type_strings[type];
}

int32_t eos_cap_get_max_id(eos_cap_type_t type) {
  switch (type) {
  case EOS_CAPS_GPIO:
#ifdef SOC_GPIO_PIN_COUNT
    return SOC_GPIO_PIN_COUNT;
#else
    return EOS_CAP_GPIO_MAX_FALLBACK;
#endif

  case EOS_CAPS_LEDC_TIMER:
#ifdef SOC_LEDC_TIMER_NUM
    return SOC_LEDC_TIMER_NUM;
#else
    return EOS_CAP_LEDC_TIMER_MAX_FALLBACK;
#endif

  case EOS_CAPS_LEDC_CHANNEL:
#ifdef SOC_LEDC_CHANNEL_NUM
    return SOC_LEDC_CHANNEL_NUM;
#else
    return EOS_CAP_LEDC_CH_MAX_FALLBACK;
#endif

  case EOS_CAPS_GDMA_CHANNEL:
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    return EOS_CAP_GDMA_CH_P4_MAX;
#else
    return EOS_CAP_GDMA_CH_S3_MAX;
#endif

  case EOS_CAPS_SPI:
#ifdef SOC_SPI_PERIPH_NUM
    return SOC_SPI_PERIPH_NUM;
#else
    return EOS_CAP_SPI_MAX_FALLBACK;
#endif

  case EOS_CAPS_I2C:
#ifdef SOC_I2C_NUM
    return SOC_I2C_NUM;
#else
    return EOS_CAP_I2C_MAX_FALLBACK;
#endif

  case EOS_CAPS_UART:
#if defined(SOC_UART_HP_NUM)
    return SOC_UART_HP_NUM;
#elif defined(SOC_UART_NUM)
    return SOC_UART_NUM;
#else
    return EOS_CAP_UART_MAX_FALLBACK;
#endif

  case EOS_CAPS_RMT:
#if defined(SOC_RMT_TX_CANDIDATES_PER_GROUP)
    return SOC_RMT_TX_CANDIDATES_PER_GROUP;
#elif defined(SOC_RMT_CHANNELS_PER_GROUP)
    return SOC_RMT_CHANNELS_PER_GROUP;
#else
    return EOS_CAP_RMT_MAX_FALLBACK;
#endif

  case EOS_CAPS_GPTIMER:
#if defined(SOC_TIMER_GROUP_TOTAL_TIMERS)
    return SOC_TIMER_GROUP_TOTAL_TIMERS;
#else
    return EOS_CAP_GPTIMER_MAX_FALLBACK;
#endif

  case EOS_CAPS_I2S:
#ifdef SOC_I2S_NUM
    return SOC_I2S_NUM;
#else
    return EOS_CAP_I2S_MAX_FALLBACK;
#endif

  case EOS_CAPS_INTR:
#if defined(SOC_CPU_INTR_NUM)
    return SOC_CPU_INTR_NUM;
#else
    return EOS_CAP_INTR_MAX_FALLBACK;
#endif

  default:
    return -1;
  }
}

static void format_owner(eos_dev_t *dev, const char *owner_label, char *out,
                         size_t out_len) {
  if (owner_label && owner_label[0] != '\0') {
    strncpy(out, owner_label, out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  if (dev) {
    if (dev->name[0] != '\0') {
      snprintf(out, out_len, "dev:%s", dev->name);
    } else if (dev->driver && dev->driver->scope[0] != '\0' && dev->driver->name[0] != '\0') {
      snprintf(out, out_len, "%s/%s", dev->driver->scope, dev->driver->name);
    } else {
      snprintf(out, out_len, "dev:%p", (void *)dev);
    }
    return;
  }
  strncpy(out, "system:reserved", out_len - 1);
  out[out_len - 1] = '\0';
}

bool eos_cap_claim_ext(eos_cap_type_t type, int32_t no, eos_dev_t *dev,
                       const char *owner_label) {
  if ((int)type < 0 || type >= EOS_CAPS_COUNT) {
    EOS_LOGE("capsmgr: Invalid capability type %d", (int)type);
    return false;
  }
  int32_t max_id = eos_cap_get_max_id(type);
  if (no < 0 || (max_id > 0 && no >= max_id)) {
    EOS_LOGW("capsmgr: Out of bounds: %s %ld (valid: 0..%ld)",
             eos_cap_type_to_str(type), (long)no, (long)(max_id - 1));
    return false;
  }

  caps_lock();

  char req_owner[EOS_CAP_OWNER_MAX];
  format_owner(dev, owner_label, req_owner, sizeof(req_owner));

  // Seek if cap already claimed
  for (size_t i = 0; i < kv_size(ecaps); i++) {
    if (kv_A(ecaps, i).type == type && kv_A(ecaps, i).no == no) {
      EOS_LOGW("capsmgr: Resource conflict on %s %ld: requested by '%s', already "
               "claimed by '%s'",
               eos_cap_type_to_str(type), (long)no, req_owner,
               kv_A(ecaps, i).owner);
      caps_unlock();
      return false;
    }
  }

  // Claim cap
  eos_cap_t newcap;
  newcap.type = type;
  newcap.no = no;
  newcap.dev = dev;
  strncpy(newcap.owner, req_owner, sizeof(newcap.owner) - 1);
  newcap.owner[sizeof(newcap.owner) - 1] = '\0';
  kv_push(eos_cap_t, ecaps, newcap);

  caps_unlock();
  return true;
}

bool eos_cap_claim(eos_cap_type_t type, int32_t no, eos_dev_t *dev) {
  return eos_cap_claim_ext(type, no, dev, NULL);
}

bool eos_cap_release(eos_cap_type_t type, int32_t no, eos_dev_t *dev) {
  caps_lock();
  for (size_t i = 0; i < kv_size(ecaps); i++) {
    if (kv_A(ecaps, i).type == type && kv_A(ecaps, i).no == no &&
        kv_A(ecaps, i).dev == dev) {
      kv_drop_fast(eos_cap_t, ecaps, i);
      kv_opt(eos_cap_t, ecaps);
      caps_unlock();
      return true;
    }
  }
  caps_unlock();
  return false;
}

size_t eos_cap_release_all_dev(eos_dev_t *dev) {
  if (!dev) {
    return 0;
  }
  caps_lock();
  size_t count = 0;
  for (size_t i = 0; i < kv_size(ecaps);) {
    if (kv_A(ecaps, i).dev == dev) {
      kv_drop_fast(eos_cap_t, ecaps, i);
      count++;
    } else {
      i++;
    }
  }
  if (count > 0) {
    kv_opt(eos_cap_t, ecaps);
    EOS_LOGI("capsmgr: Released %zu capability(ies) for dev %s", count,
             dev->name[0] != '\0' ? dev->name : "(unnamed)");
  }
  caps_unlock();
  return count;
}

bool eos_cap_is_free(eos_cap_type_t type, int32_t no) {
  caps_lock();
  for (size_t i = 0; i < kv_size(ecaps); i++) {
    if (kv_A(ecaps, i).type == type && kv_A(ecaps, i).no == no) {
      caps_unlock();
      return false;
    }
  }
  caps_unlock();
  return true;
}

bool eos_cap_get_owner(eos_cap_type_t type, int32_t no, char *out_owner,
                       size_t max_len) {
  if (!out_owner || max_len == 0) {
    return false;
  }
  caps_lock();
  for (size_t i = 0; i < kv_size(ecaps); i++) {
    if (kv_A(ecaps, i).type == type && kv_A(ecaps, i).no == no) {
      strncpy(out_owner, kv_A(ecaps, i).owner, max_len - 1);
      out_owner[max_len - 1] = '\0';
      caps_unlock();
      return true;
    }
  }
  caps_unlock();
  return false;
}

char *eos_capsmgr_dump_proc(void) {
  char *buf = NULL;
  size_t sz = 0;
  FILE *f = open_memstream(&buf, &sz);
  if (!f) {
    return NULL;
  }

  fprintf(f, "%-15s %-5s %s\n", "TYPE", "ID", "OWNER");

  caps_lock();
  for (size_t i = 0; i < kv_size(ecaps); i++) {
    fprintf(f, "%-15s %-5ld %s\n", eos_cap_type_to_str(kv_A(ecaps, i).type),
            (long)kv_A(ecaps, i).no, kv_A(ecaps, i).owner);
  }
  caps_unlock();

  fclose(f);
  return buf;
}

void eos_capsmgr_init(void) {
  if (s_caps_initialized) {
    return;
  }
  s_caps_lock = xSemaphoreCreateRecursiveMutex();
  kv_init(ecaps);
  s_caps_initialized = true;

  EOS_LOGI("capsmgr: Initializing capabilities manager and boot snapshot");

#if defined(USBPHY_DP_NUM) && defined(USBPHY_DM_NUM)
  // Claim USB related pins first so owner is recorded as system:jtag
  if (USBPHY_DP_NUM >= 0 && USBPHY_DP_NUM < SOC_GPIO_PIN_COUNT &&
      eos_cap_is_free(EOS_CAPS_GPIO, USBPHY_DP_NUM)) {
    eos_cap_claim_ext(EOS_CAPS_GPIO, USBPHY_DP_NUM, NULL, "system:jtag");
  }
  if (USBPHY_DM_NUM >= 0 && USBPHY_DM_NUM < SOC_GPIO_PIN_COUNT &&
      eos_cap_is_free(EOS_CAPS_GPIO, USBPHY_DM_NUM)) {
    eos_cap_claim_ext(EOS_CAPS_GPIO, USBPHY_DM_NUM, NULL, "system:jtag");
  }
#endif

  // Layer 1: Hardware-reserved and invalid GPIOs
  static const int32_t s_reserved_gpios[] = EOS_GPIO_RESERVED;
  for (size_t i = 0; i < sizeof(s_reserved_gpios) / sizeof(s_reserved_gpios[0]);
       i++) {
    int32_t pin = s_reserved_gpios[i];
    if (pin >= 0 && pin < SOC_GPIO_PIN_COUNT) {
      // Note: GPIO 0 is wired to input/keyboard on Lilka boards, so do not
      // claim 0 globally here
      if (pin == 0) {
        continue;
      }
      if (eos_cap_is_free(EOS_CAPS_GPIO, pin)) {
        eos_cap_claim_ext(EOS_CAPS_GPIO, pin, NULL, "system:reserved");
      }
    }
  }

  // Claim invalid and ESP-IDF reserved gpios
  for (int32_t i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
    if ((!GPIO_IS_VALID_GPIO(i)) || esp_gpio_is_reserved(BIT64(i))) {
      if (eos_cap_is_free(EOS_CAPS_GPIO, i)) {
        eos_cap_claim_ext(EOS_CAPS_GPIO, i, NULL, "system:reserved");
      }
    }
  }

  // Layer 2: Boot Snapshot of IDF peripherals
  // Console UART (only if UART console is configured)
#if defined(CONFIG_ESP_CONSOLE_UART_NUM) && (CONFIG_ESP_CONSOLE_UART_NUM >= 0)
  eos_cap_claim_ext(EOS_CAPS_UART, CONFIG_ESP_CONSOLE_UART_NUM, NULL,
                    "system:console");
#endif

  // Dedicated Flash and PSRAM SPI controller (SPI1_HOST / MSPI)
  eos_cap_claim_ext(EOS_CAPS_SPI, 0, NULL, "system:flash");
}
