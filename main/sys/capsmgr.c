#include "capsmgr.h"
#include "error.h"
#include "misc/fancymacro.h"
#include "pregen.h"

eos_cap_slot_t eos_cap_slots[EOS_MAX_CAPS];
int32_t eos_cap_slot_count = 0;

// ── Shared slot operations ─────────────────────────────────────
static bool eos_cap_claim_slot(eos_cap_t cap, int32_t cap_no,
                               eos_dev_t *owner_dev) {
  for (int32_t i = 0; i < EOS_MAX_CAPS; i++)
    if (eos_cap_slots[i].in_use && eos_cap_slots[i].cap == cap &&
        eos_cap_slots[i].cap_no == cap_no) {
      eos_errno = EOS_ERR_CAP_NO_INVALID;
      return false;
    }

  for (int32_t i = 0; i < EOS_MAX_CAPS; i++)
    if (!eos_cap_slots[i].in_use) {
      eos_cap_slots[i] = (eos_cap_slot_t){cap, cap_no, true, owner_dev};
      eos_cap_slot_count++;
      return true;
    }

  eos_errno = EOS_ERR_CAP_COUNT_QUOTA_EXCEED;
  return false;
}

// ── Per-type allocator generator ──────────────────────────────
#define EOS_CAP_ALLOC_FN(CAP_TYPE, CAP_ENUM, COUNT, RESERVED)                  \
  static const int32_t eos_cap_##CAP_TYPE##_reserved[] = RESERVED;             \
  static bool eos_cap_alloc_##CAP_TYPE(int32_t cap_no, eos_dev_t *owner_dev) { \
    if ((cap_no < 0) || (cap_no >= COUNT)) {                                   \
      eos_errno = EOS_ERR_CAP_NO_INVALID;                                      \
      return false;                                                            \
    }                                                                          \
    for (uint32_t i = 0; i < EOS_ARR_COUNT(eos_cap_##CAP_TYPE##_reserved);     \
         i++)                                                                  \
      if (cap_no == eos_cap_##CAP_TYPE##_reserved[i]) {                        \
        eos_errno = EOS_ERR_CAP_NO_RESERVED;                                   \
        return false;                                                          \
      }                                                                        \
    return eos_cap_claim_slot(CAP_ENUM, cap_no, owner_dev);                    \
  }

EOS_CAP_ALLOC_FN(gpio, EOS_CAPS_GPIO, EOS_GPIO_COUNT, EOS_GPIO_RESERVED)
#ifdef EOS_SPI_RESERVED
EOS_CAP_ALLOC_FN(spi, EOS_CAPS_SPI, EOS_SPI_COUNT, EOS_SPI_RESERVED)
#endif
#ifdef EOS_UART_RESERVED
EOS_CAP_ALLOC_FN(uart, EOS_CAPS_UART, EOS_UART_COUNT, EOS_UART_RESERVED)
#endif
#ifdef EOS_I2C_RESERVED
EOS_CAP_ALLOC_FN(i2c, EOS_CAPS_I2C, EOS_I2C_COUNT, EOS_I2C_RESERVED)
#endif

// ── Public API ─────────────────────────────────────────────────

static const int32_t eos_cap_counts[] = {
    [EOS_CAPS_GPIO] = EOS_GPIO_COUNT,
    [EOS_CAPS_I2C] = EOS_I2C_COUNT,
    [EOS_CAPS_SPI] = EOS_SPI_COUNT,
    [EOS_CAPS_UART] = EOS_UART_COUNT,
};

bool eos_cap_alloc(eos_cap_t cap, int32_t cap_no, eos_dev_t *owner_dev) {
  if (cap >= EOS_CAPS_COUNT) {
    eos_errno = EOS_ERR_CAP_INVALID;
    return false;
  }
  if (!owner_dev) {
    eos_errno = EOS_ERR_DEVICE_INVALID;
    return false;
  }
  if (eos_cap_slot_count >= EOS_MAX_CAPS) {
    eos_errno = EOS_ERR_CAP_COUNT_QUOTA_EXCEED;
    return false;
  }
  if ((cap_no < 0) || (cap_no >= eos_cap_counts[cap])) {
    eos_errno = EOS_ERR_CAP_NO_INVALID;
    return false;
  }

  switch (cap) {
  case EOS_CAPS_GPIO:
    return eos_cap_alloc_gpio(cap_no, owner_dev);
#ifdef EOS_SPI_RESERVED
  case EOS_CAPS_SPI:
    return eos_cap_alloc_spi(cap_no, owner_dev);
#else
  case EOS_CAPS_SPI:
    return eos_cap_claim_slot(EOS_CAPS_SPI, cap_no, owner_dev);
#endif
#ifdef EOS_UART_RESERVED
  case EOS_CAPS_UART:
    return eos_cap_alloc_uart(cap_no, owner_dev);
#else
  case EOS_CAPS_UART:
    return eos_cap_claim_slot(EOS_CAPS_UART, cap_no, owner_dev);
#endif
#ifdef EOS_I2C_RESERVED
  case EOS_CAPS_I2C:
    return eos_cap_alloc_i2c(cap_no, owner_dev);
#else
  case EOS_CAPS_I2C:
    return eos_cap_claim_slot(EOS_CAPS_I2C, cap_no, owner_dev);
#endif
  default:
    eos_errno = EOS_ERR_CAP_INVALID;
    return false;
  }
}

bool eos_cap_free(eos_cap_t cap, int32_t cap_no, eos_dev_t *owner_dev) {
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