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
  char scope[EOS_XSMALL_STR_LEN];
  char name[EOS_XSMALL_STR_LEN];
  // Initializes device
  bool (*init)(eos_dev_t *dev);
  // Basic IO operations:
  int (*read)(eos_dev_t *dev, void *buf, size_t len);
  int (*write)(eos_dev_t *dev, void *buf, size_t len);
  // Specific driver options whose can't be represented
  // by basic IO
  // TODO: potential ABI problems, standard to design
  int (*ioctl)(eos_dev_t *dev, int cmd, ...);

  off_t (*lseek)(eos_dev_t *dev, off_t offset, int whence);
  // Deinitializes device
  void (*shutdown)(eos_dev_t *dev);
};

//============================================(^_^)==\~

// Attribute to be used for static eos_driver_t allocation
#define EOS_DRIVER_ATTR __attribute__((section(".eos_drivers"))) const

extern const eos_driver_t _eos_drivers_start[];
extern const eos_driver_t _eos_drivers_end[];

#define EOS_DRIVER_INIT                                                        \
  .init = eos_driver_init_empty, .read = eos_driver_read_empty,                \
  .write = eos_driver_write_empty, .ioctl = eos_driver_ioctl_empty,            \
  .lseek = eos_driver_lseek_empty, .shutdown = eos_driver_shutdown_empty

// Seeks for driver with particular scope/name/pair
eos_driver_t *eos_driver_find(const char *scope, const char *name);

// DEFAULT
bool eos_driver_init_empty(eos_dev_t *dev);

int eos_driver_read_empty(eos_dev_t *dev, void *buf, size_t len);

int eos_driver_write_empty(eos_dev_t *dev, void *buf, size_t len);

int eos_driver_ioctl_empty(eos_dev_t *dev, int cmd, ...);

off_t eos_driver_lseek_empty(eos_dev_t *dev, off_t offset, int whence);

void eos_driver_shutdown_empty(eos_dev_t *dev);
