#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "includes.h"
///////////////////////////////////////////////////////
// Structure representing a device driver for EOS
///////////////////////////////////////////////////////
typedef struct eos_dev_t eos_dev_t;

typedef struct eos_driver_t eos_driver_t;
struct eos_driver_t {
  char name[EOS_SMALL_STR_LEN];
  char scope[EOS_SMALL_STR_LEN];
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

extern eos_driver_t eos_drivers[EOS_MAX_DRIVERS];

extern uint32_t eos_drivers_count;

// Driver registration macro
#define EOS_DRIVER_REG(SCOPE, NAME, INIT_ORDER)                                \
  void __attribute__((constructor(INIT_ORDER)))                                \
  eos_driver_register_##SCOPE##_##NAME(void) {                                 \
    if (eos_drivers_count >= EOS_MAX_DRIVERS) {                                \
      abort();                                                                 \
    }                                                                          \
    strcpy(eos_drivers[eos_drivers_count].name, EOS_STR(NAME));                \
    strcpy(eos_drivers[eos_drivers_count].scope, EOS_STR(SCOPE));              \
    eos_drivers[eos_drivers_count].init = driver_##SCOPE##_##NAME##_init;      \
    eos_drivers[eos_drivers_count].read = driver_##SCOPE##_##NAME##_read;      \
    eos_drivers[eos_drivers_count].write = driver_##SCOPE##_##NAME##_write;    \
    eos_drivers[eos_drivers_count].ioctl = driver_##SCOPE##_##NAME##_ioctl;    \
    eos_drivers[eos_drivers_count].shutdown =                                  \
        driver_##SCOPE##_##NAME##_shutdown;                                    \
    eos_drivers_count++;                                                       \
  }
