#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "emisc/strlimits.h"

// Filesystem capability flags
#define EOS_FS_FLAG_NONE         0U
#define EOS_FS_FLAG_PSEUDO       (1U << 0) // In-memory/pseudo filesystem (nodev in /proc/filesystems)
#define EOS_FS_FLAG_NEEDS_BDEV   (1U << 1) // Backed by storage block device (requires /dev/* source)
#define EOS_FS_FLAG_READONLY     (1U << 2) // Filesystem is inherently read-only

// Mount flags
#define EOS_MOUNT_FLAG_NONE      0U
#define EOS_MOUNT_FLAG_RDONLY    (1U << 0) // Mounted read-only

// Configuration constants
#define EOS_FS_DEFAULT_MOUNT_CAPACITY     8
#define EOS_FS_PROC_BUF_INITIAL_SIZE      512
#define EOS_FS_PROC_FILESYSTEMS_BUF_SIZE  EOS_LARGE_STR_LEN
#define EOS_FS_PROC_MOUNTS_BUF_SIZE       EOS_LARGE_STR_LEN

// Forward declarations
struct eos_fs_type;

typedef struct eos_fs_type {
  const char *name;              // Filesystem identifier ("tmpfs", "fat", "devfs", etc.)
  uint32_t flags;                // Capability flags (EOS_FS_FLAG_*)
  int (*mount)(const char *source, const char *target, uint32_t flags, const char *opts, void **out_ctx);
  int (*umount)(const char *target, void *ctx, uint32_t flags);
} eos_fs_type_t;

// Linker section macro for declarative filesystem driver registration
#define EOS_FS_ATTR __attribute__((section(".eos_fs"))) const

extern const eos_fs_type_t _eos_fs_start[];
extern const eos_fs_type_t _eos_fs_end[];

// Active mount entry record
typedef struct {
  char source[EOS_LINE_64_LEN];  // Device path ("/dev/sd0p1"), pseudo-source ("none"), etc.
  char target[EOS_LINE_64_LEN];  // Target directory prefix ("/sdcard", "/proc", etc.)
  char fstype[EOS_XSMALL_STR_LEN];// Filesystem type ("vfat", "tmpfs", "devfs", etc.)
  uint32_t flags;                // EOS_MOUNT_FLAG_*
  char opts[EOS_SMALL_STR_LEN];  // Mount options string ("rw", "ro", etc.)
  void *ctx;                     // Filesystem private context
  const eos_fs_type_t *fs;       // Pointer to registered filesystem type
  bool in_use;
} eos_mount_entry_t;

// Hook function types for mount lifecycle events
typedef void (*eos_post_mount_hook_t)(const char *source, const char *target, const char *fstype);
typedef void (*eos_pre_umount_hook_t)(const char *target);

// Subsystem initialization and discovery
void eos_fs_init(void);
const eos_fs_type_t *eos_fs_find(const char *name);

// High-level mount and unmount operations
int eos_mount(const char *source, const char *target, const char *fstype, uint32_t flags, const char *opts);
int eos_umount(const char *target_or_device, uint32_t flags);

// Status and busy query
bool eos_mount_is_busy(const char *target);
bool eos_mount_is_device_mounted(const char *device_path);
const char *eos_fs_detect_type(const char *bdev_path);

// Mount table inspection
size_t eos_fs_get_mount_count(void);
int eos_fs_get_mount_entry(size_t index, eos_mount_entry_t *out_entry);

// Lifecycle hook registration
int eos_mount_register_hooks(eos_post_mount_hook_t post_mount, eos_pre_umount_hook_t pre_umount);

// Diagnostic generator functions for procfs
char *eos_fs_generate_filesystems_proc(void);
char *eos_fs_generate_mounts_proc(void);
