#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
// Provides resources(capabilities in esp-idf) management
// by blocking theoretical pin/dma/bus/etc. usage in case
// it's already reserved by something
// (o_O)==\~

#include "pregen.h"
#include "ecore/device.h"

typedef enum {
  EOS_CAPS_GPIO,
  EOS_CAPS_I2C,
  EOS_CAPS_SPI,
  EOS_CAPS_UART,
  EOS_CAPS_PWM, // LEDC in esp idf
  EOS_CAPS_COUNT
} eos_cap_type_t;

typedef struct {
  eos_cap_type_t type;
  int32_t no;
  eos_dev_t *dev;
} eos_cap_t;

// TODO: intercept idf device inits and autoclaim caps

// Claim capability by type and index
bool eos_cap_claim(eos_cap_type_t type, int32_t no, eos_dev_t *dev);

// Releases capability
bool eos_cap_release(eos_cap_type_t type, int32_t no, eos_dev_t *dev);

// Inits capsmgr
void eos_capsmgr_init();
