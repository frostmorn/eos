#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
#include "pregen.h"
#include "sys/devtree.h"

typedef enum {
  EOS_CAPS_GPIO,
  EOS_CAPS_I2C,
  EOS_CAPS_SPI,
  EOS_CAPS_UART,
  EOS_CAPS_PWM, // LEDC in esp idf
  EOS_CAPS_COUNT
} eos_cap_t;

typedef struct {
  eos_cap_t cap;
  int32_t cap_no;
  bool in_use;
  eos_dev_t *owner_dev;
} eos_cap_slot_t;

extern eos_cap_slot_t eos_cap_slots[EOS_MAX_CAPS];

// Allocates capabity/periphereal for EOS device.
// Returns false if allocation failed and sets eos_errno in case of error
bool eos_cap_alloc(eos_cap_t cap, int32_t cap_no, eos_dev_t *owner_dev);

// Deallocates capability/peripheral for EOS device.
// Returns false if deallocation failed  and sets eos_errno in case of error
bool eos_cap_free(eos_cap_t cap, int32_t cap_no, eos_dev_t *owner_dev);

// Inits capsmgr
void eos_capsmgr_init();