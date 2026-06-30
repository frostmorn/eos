#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "includes.h"

///////////////////////////////////////////////////////
// Structure representing device in EOS dev list
///////////////////////////////////////////////////////
typedef struct eos_driver_t eos_driver_t;
struct eos_dev_t {
  // Device name for representation on a filesystem
  char name[EOS_SMALL_STR_LEN];
  // Device scope for representation on a filesystem
  char scope[EOS_SMALL_STR_LEN];
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
