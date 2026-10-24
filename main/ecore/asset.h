#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *filename;
  const uint8_t *data;
  size_t size;
} eos_asset_t;

const eos_asset_t *eos_get_init_asset(void);
