#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
#include "sys/devtree.h"
typedef enum {
  EOS_CAPS_GPIO,
  EOS_CAPS_I2C,
  EOS_CAPS_SPI,
  EOS_CAPS_PWM // LEDC in esp idf
} eos_caps_t;

typedef struct {
  eos_caps_t cap;
  uint32_t cap_index;
  eos_dev_t *dev;
} eos_caps_state_t;

// Allocates capabity/periphereal for EOS device.
// Returns false if allocation failed and sets eos_errno in case of error
bool eos_cap_alloc(eos_caps_t cap, uint32_t cap_index, eos_dev_t *dev);

// Deallocates capability/peripheral for EOS device
void eos_cap_free(eos_caps_t cap, uint32_t cap_index, eos_dev_t *dev);