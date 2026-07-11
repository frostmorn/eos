#include "capsmgr.h"
#include "error.h"
#include "misc/fancymacro.h"
#include "pregen.h"

eos_cap_slot_t eos_cap_slots[EOS_MAX_CAPS];
uint32_t eos_cap_slot_count = 0;

static const uint32_t eos_cap_reserved[][16] = {
#ifdef EOS_GPIO_RESERVED
    [EOS_CAPS_GPIO] = EOS_GPIO_RESERVED,
#endif
#ifdef EOS_I2C_RESERVED
    [EOS_CAPS_I2C] = EOS_I2C_RESERVED,
#endif
#ifdef EOS_SPI_RESERVED
    [EOS_CAPS_SPI] = EOS_SPI_RESERVED,
#endif
#ifdef EOS_UART_RESERVED
    [EOS_CAPS_UART] = EOS_UART_RESERVED,
#endif
};

static const uint32_t eos_cap_counts[] = {
#ifdef EOS_GPIO_RESERVED
    [EOS_CAPS_GPIO] = EOS_GPIO_COUNT,
#endif
#ifdef EOS_I2C_RESERVED
    [EOS_CAPS_I2C] = EOS_I2C_COUNT,
#endif
#ifdef EOS_SPI_RESERVED
    [EOS_CAPS_SPI] = EOS_SPI_COUNT,
#endif
#ifdef EOS_UART_RESERVED
    [EOS_CAPS_UART] = EOS_UART_COUNT,
#endif
};

bool eos_cap_alloc(eos_cap_t cap, uint32_t cap_no, eos_dev_t *owner_dev) {
  if (cap >= EOS_CAPS_COUNT) {
    eos_errno = EOS_ERR_CAP_INVALID;
    return false;
  }
  if (!owner_dev) {
    eos_errno = EOS_ERR_DEVICE_INVALID;
    return false;
  }
  if (cap_no >= eos_cap_counts[cap]) {
    eos_errno = EOS_ERR_CAP_NO_INVALID;
    return false;
  }
  if (eos_cap_slot_count >= EOS_MAX_CAPS) {
    eos_errno = EOS_ERR_CAP_COUNT_QUOTA_EXCEED;
    return false;
  }

  for (uint32_t i = 0; i < EOS_ARR_COUNT(eos_cap_reserved[cap]); i++)
    if (cap_no == eos_cap_reserved[cap][i]) {
      eos_errno = EOS_ERR_CAP_NO_RESERVED;
      return false;
    }

  for (uint32_t i = 0; i < EOS_MAX_CAPS; i++)
    if (eos_cap_slots[i].in_use && eos_cap_slots[i].cap == cap &&
        eos_cap_slots[i].cap_no == cap_no) {
      eos_errno = EOS_ERR_CAP_NO_INVALID;
      return false;
    }

  for (uint32_t i = 0; i < EOS_MAX_CAPS; i++)
    if (!eos_cap_slots[i].in_use) {
      eos_cap_slots[i] = (eos_cap_slot_t){cap, cap_no, true, owner_dev};
      eos_cap_slot_count++;
      return true;
    }

  eos_errno = EOS_ERR_CAP_COUNT_QUOTA_EXCEED;
  return false;
}

bool eos_cap_free(eos_cap_t cap, uint32_t cap_no, eos_dev_t *owner_dev) {
  if (!owner_dev) {
    eos_errno = EOS_ERR_DEVICE_INVALID;
    return false;
  }

  for (uint32_t i = 0; i < EOS_MAX_CAPS; i++)
    if (eos_cap_slots[i].in_use && eos_cap_slots[i].cap == cap &&
        eos_cap_slots[i].cap_no == cap_no &&
        eos_cap_slots[i].owner_dev == owner_dev) {
      bzero(&eos_cap_slots[i], sizeof(eos_cap_slot_t));
      eos_cap_slot_count--;
      return true;
    }

  eos_errno = EOS_ERR_CAP_NO_INVALID;
  return false;
}