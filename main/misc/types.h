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

static inline int32_t eos_pin_get_no(eos_pin_t *pins, const char *key) {
  if (!pins)
    return -1;
  for (eos_pin_t *p = pins; p->key != NULL; p++)
    if (strcmp(p->key, key) == 0)
      return p->val;
  return -1; // unused in esp-idf
}
