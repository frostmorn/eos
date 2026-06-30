#pragma once
#include "includes.h"
///////////////////////////////////////////////////////
// Structure representing a device driver for EOS
///////////////////////////////////////////////////////
typedef struct eos_dev_t eos_dev_t;

struct eos_driver_t {
  // Initializes device
  int (*init)(eos_dev_t *dev);
  // Basic IO operations:
  int (*read)(eos_dev_t *dev, void *buf, size_t len);
  int (*write)(eos_dev_t *dev, void *buf, size_t len);
  // Specific driver options whose can't be represented
  // by basic IO
  int (*ioctl)(eos_dev_t *dev, int cmd, va_list args);
  // Deinitializes device
  void (*shutdown)(eos_dev_t *dev);
};

//============================================(^_^)==\~


eos_driver_t eos_drivers[EOS_MAX_DRIVERS];

extern uint32_t eos_drivers_count;

// Driver registration macro
#define EOS_DRIVER_REG(SCOPE, NAME, INIT_ORDER)                                \
  static void __attribute__((constructor(INIT_ORDER)))                         \
  eos_driver_register_##SCOPE##_##NAME(void) {                                 \
    if (eos_drivers_count >= EOS_MAX_DRIVERS) {                                \
      abort();                                                                 \
    }                                                                          \
    eos_driver_t drv = {.init = driver_##SCOPE##_##NAME##_init,                \
                        .read = driver_##SCOPE##_##NAME##_read,                \
                        .write = driver_##SCOPE##_##NAME##_write,              \
                        .ioctl = driver_##SCOPE##_##NAME##_ioctl,              \
                        .shutdown = driver_##SCOPE##_##NAME##_shutdown};       \
    eos_drivers[eos_drivers_count] = drv;                                      \
    eos_drivers_count++;                                                       \
  }