#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
#include "error.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct eos_dev_t eos_dev_t;

typedef enum {
  EOS_PART_SCHEME_RAW,
  EOS_PART_SCHEME_MBR,
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

// detect partition scheme from sector 0
eos_part_scheme_t eos_diskpart_detect(eos_dev_t *dev);

// read raw sector(s) from block device
eos_error_t eos_diskpart_read_sector(eos_dev_t *dev, uint32_t lba, void *buf,
                                     uint32_t count);

// MBR
/*
// parse MBR partition table from sector 0
eos_error_t eos_diskpart_mbr_parse(eos_dev_t *dev, eos_mbr_t *out);

// get single MBR partition entry
eos_error_t eos_diskpart_mbr_get(eos_mbr_t *mbr, uint8_t idx, eos_part_t *out);

// count valid MBR partitions
uint8_t eos_diskpart_mbr_count(eos_mbr_t *mbr);

// GPT

// parse GPT header from sector 1
eos_error_t eos_diskpart_gpt_parse(eos_dev_t *dev, eos_gpt_header_t *out);

// get single GPT partition entry
eos_error_t eos_diskpart_gpt_get(eos_dev_t *dev, eos_gpt_header_t *hdr,
                                 uint32_t idx, eos_part_t *out);

// count valid GPT partitions
uint32_t eos_diskpart_gpt_count(eos_gpt_header_t *hdr);

// FS Detect

// detect filesystem type on a partition
eos_fs_type_t eos_diskpart_fs_detect(eos_dev_t *dev, eos_part_t *part);

// map partition type code to fs type
eos_fs_type_t eos_diskpart_type_to_fs(uint8_t type_code);

*/