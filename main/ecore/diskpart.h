#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// Max value for mbr, and practically we would work mostly with it
#define EOS_MAX_PARTITIONS 4

#include "error.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct eos_dev_t eos_dev_t;

typedef enum {
  EOS_PART_SCHEME_RAW,
  EOS_PART_SCHEME_MBR, // Also known as DOS
  EOS_PART_SCHEME_GPT,
  EOS_PART_SCHEME_IDF, // Would be good to support that one as well
  EOS_PART_SCHEME_UNKNOWN,
} eos_part_scheme_t;

// Converts part scheme to str
const char *eos_part_scheme_str(eos_part_scheme_t scheme);

// https://en.wikipedia.org/wiki/Partition_type
typedef enum {
  // ── Empty ─────────────────────────────────────────────────
  EOS_PART_EMPTY = 0x00, // Unused entry

  // ── FAT ───────────────────────────────────────────────────
  EOS_PART_FAT12 = 0x01,            // FAT12 (<32MB)
  EOS_PART_FAT16_LT32MB = 0x04,     // FAT16 (<32MB)
  EOS_PART_FAT16B = 0x06,           // FAT16B (>=32MB)
  EOS_PART_FAT32 = 0x0B,            // FAT32 CHS
  EOS_PART_FAT32_LBA = 0x0C,        // FAT32 LBA
  EOS_PART_FAT16_LBA = 0x0E,        // FAT16 LBA
  EOS_PART_FAT12_HIDDEN = 0x11,     // Hidden FAT12
  EOS_PART_FAT16_HIDDEN = 0x14,     // Hidden FAT16 <32MB
  EOS_PART_FAT16B_HIDDEN = 0x16,    // Hidden FAT16B
  EOS_PART_FAT32_HIDDEN = 0x1B,     // Hidden FAT32
  EOS_PART_FAT32_LBA_HIDDEN = 0x1C, // Hidden FAT32 LBA
  EOS_PART_FAT16_LBA_HIDDEN = 0x1E, // Hidden FAT16 LBA

  // ── Extended ──────────────────────────────────────────────
  EOS_PART_EXTENDED_CHS = 0x05,    // Extended CHS
  EOS_PART_EXTENDED_LBA = 0x0F,    // Extended LBA
  EOS_PART_EXTENDED_HIDDEN = 0x15, // Hidden Extended CHS
  EOS_PART_EXTENDED_LBA2 = 0x1F,   // Hidden Extended LBA

  // ── NTFS / exFAT / IFS ────────────────────────────────────
  EOS_PART_NTFS_EXFAT = 0x07,  // NTFS / exFAT / IFS
  EOS_PART_NTFS_HIDDEN = 0x17, // Hidden NTFS
  EOS_PART_WINRE = 0x27,       // Windows Recovery

  // ── Linux ─────────────────────────────────────────────────
  EOS_PART_LINUX_SWAP = 0x82,      // Linux swap
  EOS_PART_LINUX_FS = 0x83,        // Linux filesystem (ext2/3/4/xfs...)
  EOS_PART_LINUX_EXTENDED = 0x85,  // Linux extended
  EOS_PART_LINUX_LVM = 0x8E,       // Linux LVM
  EOS_PART_LINUX_RAID = 0xFD,      // Linux RAID autodetect
  EOS_PART_LINUX_FS_HIDDEN = 0x93, // Hidden Linux filesystem

  // ── BSD ───────────────────────────────────────────────────
  EOS_PART_FREEBSD = 0xA5,   // FreeBSD
  EOS_PART_OPENBSD = 0xA6,   // OpenBSD
  EOS_PART_NETBSD = 0xA9,    // NetBSD
  EOS_PART_DRAGONFLY = 0x6C, // DragonFly BSD

  // ── macOS ─────────────────────────────────────────────────
  EOS_PART_MACOS_X = 0xA8,      // macOS X
  EOS_PART_MACOS_X_BOOT = 0xAB, // macOS X boot
  EOS_PART_MACOS_X_HFS = 0xAF,  // macOS X HFS+

  // ── Windows dynamic / LDM ─────────────────────────────────
  EOS_PART_WIN_DYNAMIC = 0x42,  // Windows dynamic disk
  EOS_PART_WIN_LDM_META = 0x5C, // Windows LDM metadata
  EOS_PART_WIN_LDM_DATA = 0x5D, // Windows LDM data (span/stripe)
  EOS_PART_WIN_RECOVERY = 0xDE, // Dell recovery
  EOS_PART_IBM_RECOVERY = 0x12, // Compaq/IBM diagnostics

  // ── EFI / GPT ─────────────────────────────────────────────
  EOS_PART_GPT_PROTECTIVE = 0xEE, // GPT protective MBR
  EOS_PART_EFI_SYSTEM = 0xEF,     // EFI system partition

  // ── Solaris / Unix ────────────────────────────────────────
  EOS_PART_SOLARIS_X86 = 0xBF,  // Solaris x86
  EOS_PART_SOLARIS_BOOT = 0xBE, // Solaris boot
  EOS_PART_UNIX_SYS_V = 0x63,   // Unix System V

  // ── QNX ───────────────────────────────────────────────────
  EOS_PART_QNX4_P1 = 0x4D, // QNX4 primary
  EOS_PART_QNX4_P2 = 0x4E, // QNX4 secondary
  EOS_PART_QNX4_P3 = 0x4F, // QNX4 tertiary

  // ── Misc ──────────────────────────────────────────────────
  EOS_PART_OS2_BOOT_MGR = 0x0A, // OS/2 Boot Manager
  EOS_PART_INTEL_RST = 0x84,    // Intel Rapid Start / hibernation
  EOS_PART_LUKS = 0xE8,         // Linux LUKS encrypted
  EOS_PART_VMWARE_VMFS = 0xFB,  // VMware VMFS
  EOS_PART_VMWARE_SWAP = 0xFC,  // VMware swap
  EOS_PART_XENIX_ROOT = 0x02,   // XENIX root
  EOS_PART_XENIX_USR = 0x03,    // XENIX usr
  EOS_PART_PLAN9 = 0x39,        // Plan 9

  // ── EOS internal (not MBR type codes) ─────────────────────
  EOS_PART_WHOLE_DISK = 0x100, // RAW scheme — whole device
  EOS_PART_GPT_ENTRY = 0x101,  // GPT partition (type from GUID)
  EOS_PART_UNKNOWN = 0xFF,
} eos_part_type_t;

// Converts part type to cstr
const char *eos_part_type_str(eos_part_type_t type);

typedef struct {
  uint32_t lba_start;
  uint32_t lba_size;
  eos_part_type_t part_type;
  bool bootable;
} eos_part_t;

typedef struct {
  eos_dev_t *dev;
  eos_part_scheme_t scheme;
  uint32_t count;
  eos_part_t parts[EOS_MAX_PARTITIONS];
} eos_diskpart_t;

// detect what's on the device and how many partitions
eos_error_t eos_diskpart_open(eos_dev_t *dev, eos_diskpart_t **out);

// get partition count
uint32_t eos_diskpart_count(eos_diskpart_t *dp);

// get partition info by index
eos_error_t eos_diskpart_get(eos_diskpart_t *dp, uint32_t idx, eos_part_t *out);

// free resources
void eos_diskpart_close(eos_diskpart_t *dp);

// ── Partition table creation ──────────────────────────────────

// initialize a fresh MBR partition table on device
eos_error_t eos_diskpart_create(eos_dev_t *dev, eos_part_scheme_t scheme,
                                eos_diskpart_t **out);

// ── Partition management ──────────────────────────────────────

// add a new partition
eos_error_t eos_diskpart_add(eos_diskpart_t *dp, uint32_t lba_start,
                             uint32_t lba_size, eos_part_type_t part_type);

// remove partition by index
eos_error_t eos_diskpart_remove(eos_diskpart_t *dp, uint32_t idx);

// write partition table back to device
eos_error_t eos_diskpart_commit(eos_diskpart_t *dp);

// calculate free/unpartitioned space
eos_error_t eos_diskpart_free_space(eos_diskpart_t *dp, uint32_t *lba_start_out,
                                    uint32_t *lba_size_out);