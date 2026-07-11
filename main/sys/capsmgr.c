#include "capsmgr.h"
#include "error.h"
#include "misc/fancymacro.h"
#include "pregen.h"

eos_cap_slot_t eos_cap_slots[EOS_MAX_CAPS];
uint32_t eos_cap_slot_count = 0;

// GPIO:
static const uint32_t eos_cap_gpio_reserved[] = EOS_GPIO_RESERVED;
static bool eos_cap_alloc_gpio(uint32_t cap_no, eos_dev_t *owner_dev) {
  // Check Args
  if (cap_no > EOS_GPIO_COUNT) {
    eos_errno = EOS_ERR_CAP_NO_INVALID;
    return false;
  }

  for (uint32_t i = 0; i < EOS_ARR_COUNT(eos_cap_gpio_reserved); i++) {
    if (cap_no == eos_cap_gpio_reserved[i]) {
      eos_errno = EOS_ERR_CAP_NO_RESERVED;
      return false;
    }
  }
}

// I2C
static const uint32_t eos_cap_i2c_reserved[] = EOS_I2C_RESERVED;
static bool eos_cap_alloc_i2c(uint32_t cap_no, eos_dev_t *owner_dev) {}

static const uint32_t eos_cap_spi_reserved[] = EOS_SPI_RESERVED;
static bool eos_cap_alloc_spi(uint32_t cap_no, eos_dev_t *owner_dev) {}

static bool eos_cap_alloc_uart(uint32_t cap_no, eos_dev_t *owner_dev) {}

bool eos_cap_alloc(eos_cap_t cap, uint32_t cap_no, eos_dev_t *owner_dev) {
  // Args check
  if (cap >= EOS_CAPS_COUNT) {
    eos_errno = EOS_ERR_CAP_INVALID;
    return false;
  }

  if (eos_cap_slot_count >= EOS_MAX_CAPS - 1) {
    eos_errno = EOS_ERR_CAP_COUNT_QUOTA_EXCEED;
    return false;
  }

  if (!owner_dev) {
    return EOS_ERR_DEVICE_INVALID;
  }

  switch (cap) {
  case EOS_CAPS_GPIO: {
    return eos_cap_alloc_gpio(cap_no, owner_dev);
    break;
  }
  }
}

bool eos_cap_free(eos_cap_t cap, uint32_t cap_no, eos_dev_t *owner_dev) {}