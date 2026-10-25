#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "emisc/strlimits.h"
#include <stdint.h>

#define EOS_BIN_MAGIC 0x0C0A00AC

// Bin manifest flags
#define EOS_BIN_FLAG_NONE   0U
#define EOS_BIN_FLAG_HIDDEN (1U << 0) // Hide application from GUI launcher menu
#define EOS_BIN_FLAG_GUI    (1U << 1) // Application uses framebuffer/GUI directly

// Default, minimum, and GUI thread stack sizes for ebin execution
#define EOS_BIN_DEFAULT_STACK_SIZE 8192U
#define EOS_BIN_MIN_STACK_SIZE     4096U
#define EOS_BIN_GUI_STACK_SIZE     24576U

// Bin manifest
typedef struct {
  // Magic number which allows us to distingiush eos bin from something else
  uint32_t magic;
  // Various flags whose may or may not influence an bin launch
  uint32_t flags;
  // Desired thread stack size in bytes (0 = use EOS_BIN_DEFAULT_STACK_SIZE)
  uint32_t stack_size;
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
  .magic = EOS_BIN_MAGIC, .flags = EOS_BIN_FLAG_NONE,                          \
  .stack_size = EOS_BIN_DEFAULT_STACK_SIZE, .filename = "", .name = "",        \
  .group = "", .description = "", .entry_point = eos_bin_main

#define EOS_BIN_ATTR __attribute__((section(".eos_bins"))) const

// Returns validated stack size for an ebin manifest
static inline uint32_t eos_bin_effective_stack_size(const eos_bin_t *bin) {
  if (!bin || bin->stack_size == 0) {
    return EOS_BIN_DEFAULT_STACK_SIZE;
  }
  return (bin->stack_size < EOS_BIN_MIN_STACK_SIZE) ? EOS_BIN_MIN_STACK_SIZE
                                                    : bin->stack_size;
}

// our list of bins here (^____^)==\~
extern const eos_bin_t _eos_bins_start[];
extern const eos_bin_t _eos_bins_end[];

// Empty bin entry point implementation
int eos_bin_main(int argc, char **argv);
