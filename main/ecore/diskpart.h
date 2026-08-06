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
  EOS_PART_SCHEME_UNKNOWN,
} eos_part_scheme_t;

extern const char EOS_PARTSCHEME_CSTR[][8];

typedef enum {
  EOS_FS_TYPE_UNKNOWN,
  EOS_FS_TYPE_FAT12,
  EOS_FS_TYPE_FAT16,
  EOS_FS_TYPE_FAT32,
  EOS_FS_TYPE_EXFAT,
  EOS_FS_TYPE_NTFS,
  EOS_FS_TYPE_EXT,
  EOS_FS_TYPE_RAW,
} eos_fs_type_t;

typedef struct {
  uint32_t lba_start;
  uint32_t lba_size;
  uint8_t type_code;
  eos_fs_type_t fs_type;
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
                             uint32_t lba_size, eos_fs_type_t fs_type);

// remove partition by index
eos_error_t eos_diskpart_remove(eos_diskpart_t *dp, uint32_t idx);

// write partition table back to device
eos_error_t eos_diskpart_commit(eos_diskpart_t *dp);

// calculate free/unpartitioned space
eos_error_t eos_diskpart_free_space(eos_diskpart_t *dp, uint32_t *lba_start_out,
                                    uint32_t *lba_size_out);