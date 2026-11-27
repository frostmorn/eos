#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "emisc/strlimits.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EOS_BIN_MAGIC 0x0C0A00AC

// Bin manifest flags
#define EOS_BIN_FLAG_NONE         0U
#define EOS_BIN_FLAG_HIDDEN       (1U << 0) // Hide application from GUI launcher menu
#define EOS_BIN_FLAG_GUI          (1U << 1) // Application uses framebuffer/GUI directly
#define EOS_BIN_FLAG_STACK_SRAM   (1U << 2) // Force stack in internal SRAM
#define EOS_BIN_FLAG_STACK_PSRAM  (1U << 3) // Prefer stack in external PSRAM
#define EOS_BIN_FLAG_ROLE_SHIFT   4U
#define EOS_BIN_FLAG_ROLE_MASK    (3U << EOS_BIN_FLAG_ROLE_SHIFT)
#define EOS_BIN_FLAG_ROLE_FULL    (1U << EOS_BIN_FLAG_ROLE_SHIFT)
#define EOS_BIN_FLAG_ROLE_STATUS  (2U << EOS_BIN_FLAG_ROLE_SHIFT)
#define EOS_BIN_FLAG_ROLE_TOOLBAR (3U << EOS_BIN_FLAG_ROLE_SHIFT)

// Default, minimum, and GUI thread stack sizes for ebin execution
#define EOS_BIN_DEFAULT_STACK_SIZE 8192U
#define EOS_BIN_MIN_STACK_SIZE     4096U
#define EOS_BIN_LARGE_STACK_SIZE   12288U
#define EOS_BIN_GUI_STACK_SIZE     16384U

#define EOS_BIN_ABI_VERSION 1U

// Bin manifest
typedef struct {
  // Magic number which allows us to distinguish eos bin from something else
  uint32_t magic;
  // Size of the manifest structure for forward/backward compatibility
  uint32_t struct_size;
  // ABI version of manifest (EOS_BIN_ABI_VERSION)
  uint32_t abi_version;
  // Minimum OS API level required by binary (0 = any)
  uint32_t min_api;
  // Requested display color format (EOS_DISPLAY_FMT_*, 0 = default/inherit)
  uint32_t display_format;
  // Required system capability bitmask (reserved for permission checks)
  uint32_t caps_required;
  // Reserved words for future extensions
  uint32_t reserved[2];
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
  // Minimum dimensions required by binary
  uint16_t min_w;
  uint16_t min_h;
  // Default toolbar hint text
  char hint[EOS_MID_STR_LEN];
  // bin entry point
  int (*entry_point)(int argc, char **argv);
} eos_bin_t;

#define EOS_BIN_INITIALIZER                                                    \
  .magic = EOS_BIN_MAGIC, .struct_size = sizeof(eos_bin_t),                   \
  .abi_version = EOS_BIN_ABI_VERSION, .min_api = 0, .display_format = 0,       \
  .caps_required = 0, .reserved = {0, 0}, .flags = EOS_BIN_FLAG_NONE,          \
  .stack_size = EOS_BIN_DEFAULT_STACK_SIZE, .filename = "", .name = "",        \
  .group = "", .description = "", .min_w = 0, .min_h = 0, .hint = "",         \
  .entry_point = eos_bin_main

#define EOS_BIN_ATTR __attribute__((section(".eos_bins"))) const

// Returns true if binary stack should be allocated from PSRAM
static inline bool eos_bin_prefers_psram_stack(const eos_bin_t *bin) {
  if (!bin) return false;
  if (bin->flags & EOS_BIN_FLAG_STACK_SRAM) return false;
  return ((bin->flags & EOS_BIN_FLAG_STACK_PSRAM) || (bin->flags & EOS_BIN_FLAG_GUI));
}

// Returns validated stack size for an ebin manifest
static inline uint32_t eos_bin_effective_stack_size(const eos_bin_t *bin) {
  if (!bin) {
    return EOS_BIN_DEFAULT_STACK_SIZE;
  }
  if (bin->stack_size == 0) {
    return (bin->flags & EOS_BIN_FLAG_GUI) ? EOS_BIN_GUI_STACK_SIZE
                                           : EOS_BIN_DEFAULT_STACK_SIZE;
  }
  return (bin->stack_size < EOS_BIN_MIN_STACK_SIZE) ? EOS_BIN_MIN_STACK_SIZE
                                                    : bin->stack_size;
}

// Configure ESP-IDF pthread settings before calling pthread_create() based on ebin manifest
void eos_pthread_set_manifest_cfg(const eos_bin_t *bin);
void eos_pthread_set_stack_cfg(uint32_t stack_size, bool prefer_psram, const char *name);

// our list of bins here (^____^)==\~
extern const eos_bin_t _eos_bins_start[];
extern const eos_bin_t _eos_bins_end[];

// Canonical executable / script PATH resolution helper
int eos_resolve_path(const char *name, char *out, size_t out_len);

// Empty bin entry point implementation
int eos_bin_main(int argc, char **argv);
