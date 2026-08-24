#include "capsmgr.h"
#include "ecore/driver.h"
#include "emisc/fancymacro.h"
#include "emisc/kvec.h"
#include "error.h"
#include <esp_bit_defs.h>
#include <soc/soc_caps.h>

// consider using bitmask for caps
extern bool esp_gpio_is_reserved(uint64_t bitmask);

static kvec_t(eos_cap_t) ecaps;

static const char *eos_cap_type_strings[] = {[EOS_CAPS_GPIO] = "GPIO",
                                             [EOS_CAPS_I2C] = "I2C",
                                             [EOS_CAPS_SPI] = "SPI",
                                             [EOS_CAPS_UART] = "UART",
                                             [EOS_CAPS_PWM] = "PWM(LEDC)"};

char *eos_cap_type_to_str(eos_cap_type_t type) {
  if (type >= sizeof(eos_cap_type_strings) / sizeof(*eos_cap_type_strings))
    return "unknown error";
  return (char *)eos_cap_type_strings[type];
}

bool eos_cap_claim(eos_cap_type_t type, int32_t no, eos_dev_t *dev) {
  // Invalid vap type
  if (type < 0 || type >= EOS_CAPS_COUNT) {
    EOS_LOGE("Wrong cap used\n");
    abort();
  }

  // Invalid cap index
  if (no < 0)
    return false;

  // Seek if cap already claimed
  for (size_t i = 0; i < kv_size(ecaps); i++) {
    if ((kv_A(ecaps, i).type == type) && (kv_A(ecaps, i).no == no))
      return false;
  }

  // Claiming cap
  eos_cap_t newcap = {.type = type, .no = no, .dev = dev};
  kv_push(eos_cap_t, ecaps, newcap);

  EOS_LOGI("Claimed cap %s %d by dev %s/%s", eos_cap_type_to_str(type), no,
           dev ? dev->driver->scope : "NULL", dev ? dev->driver->name : "NULL");

  return true;
}

bool eos_cap_release(eos_cap_type_t type, int32_t no, eos_dev_t *dev) {
  for (size_t i = 0; i < kv_size(ecaps); i++) {
    if ((kv_A(ecaps, i).type == type) && (kv_A(ecaps, i).no == no) &&
        (kv_A(ecaps, i).dev == dev)) {
      kv_drop_fast(eos_cap_t, ecaps, i);
      kv_opt(eos_cap_t, ecaps);
      return true;
    }
  }
  return false;
}
// soc/gpio_sig_map.h per soc
void eos_capsmgr_init() {
  kv_init(ecaps);

  // Claim reserved gpios
  for (int32_t i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
    if (esp_gpio_is_reserved(BIT64(i)))
      eos_cap_claim(EOS_CAPS_GPIO, i, NULL);
  }
}
