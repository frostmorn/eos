#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "emisc/strlimits.h"
#include <stdint.h>

#define EOS_BIN_MAGIC 0x0C0A00AC


// Bin manifest
typedef struct {
  // Magic number which allows us to distingiush eos bin from something else
  uint32_t magic;
  // Various flags whose may or may not influence an bin launch
  uint32_t flags;
  // filename used to expose file via binfs
  char filename[EOS_SMALL_STR_LEN];
  // name to be used in GUI menus, etc.
  char name[EOS_SMALL_STR_LEN];
  // group to be used in GUI menus, etc
  char group[EOS_XSMALL_STR_LEN];
  // description to be used in GUI menus, etc.
  char description[EOS_MID_STR_LEN];
  // bin entry point
  int (*entry_point)(int argc, char **argv);
} eos_bin_t;

#define EOS_BIN_INITIALIZER                                                    \
  .magic = EOS_BIN_MAGIC, .flags = 0, .filename = "", .name = "", .group = "",      \
  .description = "", .entry_point = eos_bin_main

#define EOS_BIN_ATTR __attribute__((section(".eos_bins"))) const

// our list of bins here (^____^)==\~
extern const eos_bin_t _eos_bins_start[];
extern const eos_bin_t _eos_bins_end[];

// Empty bin entry point implementation
int eos_bin_main(int argc, char **argv);
