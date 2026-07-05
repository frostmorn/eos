#pragma once
#include "includes.h"

// Pin representation
typedef struct {
  char *key;
  int32_t val;
} eos_pin_t;

// Additional settings
typedef struct {
  char *key;
  char *val;
} eos_cfg_t;