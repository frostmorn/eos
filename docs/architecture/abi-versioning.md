# EOS ABI Versioning and Binary Manifest Policy

## 1. Overview

EOS defines binary manifests (`eos_bin_t`) for built-in commands (`ebin`) and dynamic loadable ELF binaries (`T08`). The Application Binary Interface (ABI) guarantees stability, forward and backward compatibility, and runtime validation across OS updates and third-party binaries.

---

## 2. Manifest Versioning (`eos_bin_t`)

The binary manifest is placed in the `.eos_bins` section (for firmware binaries) or embedded inside an ELF binary section (for dynamic binaries).

### Structure Layout (Manifest v2)

```c
typedef struct {
  uint32_t magic;          // EOS_BIN_MAGIC (0x0C0A00AC)
  uint32_t struct_size;    // sizeof(eos_bin_t)
  uint32_t abi_version;    // EOS_BIN_ABI_VERSION (currently 2)
  uint32_t min_api;        // Minimum required OS API level (0 = any)
  uint32_t display_format; // Requested eos_display_format_t (0 = default/inherit)
  uint32_t caps_required;  // Capability bitmask required by binary
  uint32_t reserved[2];    // Reserved for future extensions
  uint32_t flags;          // EOS_BIN_FLAG_*
  uint32_t stack_size;     // Desired stack size in bytes (0 = default)
  char filename[EOS_SMALL_STR_LEN];
  char name[EOS_SMALL_STR_LEN];
  char group[EOS_XSMALL_STR_LEN];
  char description[EOS_MID_STR_LEN];
  uint16_t min_w;
  uint16_t min_h;
  char hint[EOS_MID_STR_LEN];
  int (*entry_point)(int argc, char **argv);
} eos_bin_t;
```

### Compatibility Rules:
1. **Size-Prefixing (`struct_size`)**: The loader reads `struct_size`. If an older binary with a smaller `struct_size` is executed on a newer kernel, fields beyond `struct_size` default to 0 / neutral.
2. **ABI Versions**:
   - `EOS_BIN_ABI_VERSION_1`: Legacy manifest without size/display_format headers.
   - `EOS_BIN_ABI_VERSION_2`: Explicit versioning with `struct_size`, `abi_version`, `min_api`, `display_format`, `caps_required`, and reserved words.
3. **`EOS_BIN_INITIALIZER`**: Any new field must be initialized with default values in `EOS_BIN_INITIALIZER` so that existing C source manifests continue to compile without modification.

---

## 3. Breaking Changes vs Non-Breaking Extensions

### Non-Breaking Changes (No ABI Bump):
- Adding new optional fields to the end of `eos_bin_t` (or utilizing `reserved[]`), guarded by `struct_size`.
- Adding new syscalls / VFS functions to the end of the export table (`T08`).
- Adding new IOCTL commands or new filesystem types.

### Breaking Changes (Requires `EOS_BIN_ABI_VERSION` Bump):
- Modifying the offset, size, or interpretation of existing fields.
- Removing or renumbering syscall numbers, IOCTL commands, or exported symbol signatures.
- Changing calling conventions or pointer bit widths.

---

## 4. Export Table Versioning (`T08`)

The dynamic loader (`ecore/elf_symbols.c`) exposes kernel and POSIX functions to dynamically loaded ELFs:
- The symbol table is strictly append-only.
- New symbols are registered without altering existing indices.
- Removal of exported symbols is forbidden; deprecated symbols are aliased or preserved with warnings.
