#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "includes.h"
#include <esp_vfs.h>
///////////////////////////////////////////////////////
// Structure representing a device driver for EOS
///////////////////////////////////////////////////////
typedef struct eos_dev_t eos_dev_t;

typedef struct eos_driver_t eos_driver_t;
struct eos_driver_t {
  char scope[EOS_SMALL_STR_LEN];
  char name[EOS_SMALL_STR_LEN];
  // Initializes device
  bool (*init)(eos_dev_t *dev);
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

extern eos_driver_t *eos_driver_slots[EOS_MAX_DRIVERS];

// Compile time driver registration macro
#define EOS_DRIVER_REG(DRV, INIT_ORDER)                                        \
  void __attribute__((constructor(INIT_ORDER))) eos_driver_register_##DRV(     \
      void) {                                                                  \
    eos_error_t err = eos_driver_reg(&(DRV));                                  \
    if (err != EOS_ERR_NO_ERROR)                                               \
      EOS_LOGE("%s", eos_error_to_str(err));                                   \
  }

#define EOS_DRIVER_ATTR

extern eos_driver_t *eos_driver_find(const char *scope, const char *name);

extern eos_error_t eos_driver_reg(eos_driver_t *driver);

// Default placeholders for cases when driver do not implement one or more
// calls;
extern bool eos_driver_init_empty(eos_dev_t *dev);
extern int eos_driver_read_empty(eos_dev_t *dev, void *buf, size_t len);
extern int eos_driver_write_empty(eos_dev_t *dev, void *buf, size_t len);
extern int eos_driver_ioctl_empty(eos_dev_t *dev, int cmd, ...);
extern void eos_driver_shutdown_empty(eos_dev_t *dev);