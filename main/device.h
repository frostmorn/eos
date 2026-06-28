#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
#include "includes.h"
#include "strlimits.h"

///////////////////////////////////////////////////////
// Structure representing a device driver for EOS
///////////////////////////////////////////////////////
typedef struct eos_dev_t eos_dev_t;
typedef struct eos_driver_t eos_driver_t;
struct eos_driver_t{
 // Initializes device
 int (*init) (eos_dev_t *dev);
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
struct eos_dev_t{
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

#define EOS_DEV_TYPE_ROOT      0
#define EOS_DEV_TYPE_CPU       1
#define EOS_DEV_TYPE_MEMORY    2
#define EOS_DEV_TYPE_STORAGE   3
#define EOS_DEV_TYPE_DISPLAY   4
#define EOS_DEV_TYPE_BUTTON    5
#define EOS_DEV_TYPE_SENSOR    6  

eos_driver_t eos_drivers[EOS_DRIVER_COUNT];
