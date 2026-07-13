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
  int (*ioctl)(eos_dev_t *dev, int cmd, ...);
  // Deinitializes device
  void (*shutdown)(eos_dev_t *dev);
};

//============================================(^_^)==\~

extern eos_driver_t eos_driver_slots[EOS_MAX_DRIVERS];

extern uint32_t eos_driver_slot_count;

// Driver registration macro
#define EOS_DRIVER_REG(SCOPE, NAME, INIT_ORDER)                                \
  void __attribute__((constructor(INIT_ORDER)))                                \
  eos_driver_register_##SCOPE##_##NAME(void) {                                 \
    if (eos_driver_slot_count >= EOS_MAX_DRIVERS) {                                \
      EOS_LOGE("Not enough slots to init driver for %s/%s\n", EOS_STR(SCOPE),  \
               EOS_STR(NAME));                                                 \
      abort();                                                                 \
    }                                                                          \
    strcpy(eos_driver_slots[eos_driver_slot_count].name, EOS_STR(NAME));                \
    strcpy(eos_driver_slots[eos_driver_slot_count].scope, EOS_STR(SCOPE));              \
    eos_driver_slots[eos_driver_slot_count].init = driver_##SCOPE##_##NAME##_init;      \
    eos_driver_slots[eos_driver_slot_count].read = driver_##SCOPE##_##NAME##_read;      \
    eos_driver_slots[eos_driver_slot_count].write = driver_##SCOPE##_##NAME##_write;    \
    eos_driver_slots[eos_driver_slot_count].ioctl = driver_##SCOPE##_##NAME##_ioctl;    \
    eos_driver_slots[eos_driver_slot_count].shutdown =                                  \
        driver_##SCOPE##_##NAME##_shutdown;                                    \
    eos_driver_slot_count++;                                                       \
  }

extern eos_driver_t *eos_driver_find(char *scope, char *name);