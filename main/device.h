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
  // Initializes device
  int (*init)(eos_dev_t *dev);
  // Basic IO operations:
  int (*read)(eos_dev_t *dev, void *buf, size_t len);
  int (*write)(eos_dev_t *dev, void *buf, size_t len);
  // Specific device options whose can't be represented
  // by basic IO
  int (*ioctl)(eos_dev_t *dev, int cmd, va_list args);
  // Deinitializes device
  void (*shutdown)(eos_dev_t *dev);
};

//============================================(^_^)==\~

///////////////////////////////////////////////////////
// Structure representing device in EOS dev list
///////////////////////////////////////////////////////
struct eos_dev_t {
  // Device name for representation on a filesystem
  char name[EOS_SMALL_STR_LEN];
  // EOS_DEV_TYPE_
  uint8_t type;
  // Unique identifier in a system
  uint32_t id;
  // Size of device data in bytes
  uint32_t size;
  // Pointer to driver working with this device
  eos_driver_t *driver;
};
//============================================(^_^)==\~

eos_driver_t eos_drivers[EOS_MAX_DRIVERS];

extern uint32_t eos_drivers_count;

// Driver registration macro
#define EOS_DRIVER_REG(SCOPE, NAME)                                            \
  static void __attribute__((constructor(EOS_INIT_DRIVERS_PRIO)))              \
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