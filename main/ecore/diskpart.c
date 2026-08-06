#include "diskpart.h"
#include "ecore/device.h"
#include "emisc/fancymacro.h"
#include <stdlib.h>
#include <string.h>
#include "ecore/driver.h"
#include "ecore/device.h"

// ── String representations ────────────────────────────────────

const char EOS_PARTSCHEME_CSTR[][8] = {
    [EOS_PART_SCHEME_RAW] = "RAW",
    [EOS_PART_SCHEME_MBR] = "MBR",
    [EOS_PART_SCHEME_GPT] = "GPT",
    [EOS_PART_SCHEME_UNKNOWN] = "UNKNOWN",
};

// ── MBR structures ────────────────────────────────────────────

#define MBR_SIGNATURE 0xAA55
#define MBR_SIG_OFFSET 510
#define GPT_PROTECTIVE 0xEE
#define SECTOR_SIZE 512

typedef struct __attribute__((packed)) {
  uint8_t status;
  uint8_t chs_first[3];
  uint8_t type;
  uint8_t chs_last[3];
  uint32_t lba_start;
  uint32_t lba_size;
} mbr_entry_t;

typedef struct __attribute__((packed)) {
  uint8_t bootstrap[446];
  mbr_entry_t entries[4];
  uint16_t signature;
} mbr_t;

// ── GPT structures ────────────────────────────────────────────

#define GPT_HEADER_LBA 1
#define GPT_SIGNATURE 0x5452415020494645ULL // "EFI PART"

typedef struct __attribute__((packed)) {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t header_crc32;
  uint32_t reserved;
  uint64_t my_lba;
  uint64_t alt_lba;
  uint64_t first_usable_lba;
  uint64_t last_usable_lba;
  uint8_t disk_guid[16];
  uint64_t part_entry_lba;
  uint32_t num_part_entries;
  uint32_t part_entry_size;
  uint32_t part_array_crc32;
} gpt_header_t;

typedef struct __attribute__((packed)) {
  uint8_t type_guid[16];
  uint8_t unique_guid[16];
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attributes;
  uint16_t name[36]; // UTF-16LE
} gpt_entry_t;

// GPT type GUIDs that indicate used partitions (non-empty)
static const uint8_t GPT_UNUSED_GUID[16] = {0};

// ── Internal: sector I/O ──────────────────────────────────────

static eos_error_t read_sector(eos_dev_t *dev, uint32_t lba, void *buf,
                               uint32_t count) {
  // seek to LBA
  off_t offset = (off_t)lba * SECTOR_SIZE;
  if (dev->driver->lseek(dev, offset, SEEK_SET) != offset)
    return EOS_ERR_DEVICE_INVALID;

  // read sectors
  size_t bytes = (size_t)count * SECTOR_SIZE;
  if (dev->driver->read(dev, buf, bytes) != (int)bytes)
    return EOS_ERR_DEVICE_INVALID;

  return EOS_ERR_NO_ERROR;
}

static eos_error_t write_sector(eos_dev_t *dev, uint32_t lba, const void *buf,
                                uint32_t count) {
  off_t offset = (off_t)lba * SECTOR_SIZE;
  if (dev->driver->lseek(dev, offset, SEEK_SET) != offset)
    return EOS_ERR_DEVICE_INVALID;

  size_t bytes = (size_t)count * SECTOR_SIZE;
  if (dev->driver->write(dev, (void *)buf, bytes) != (int)bytes)
    return EOS_ERR_DEVICE_INVALID;

  return EOS_ERR_NO_ERROR;
}

// ── Internal: type code → fs type ─────────────────────────────

static eos_fs_type_t type_code_to_fs(uint8_t type) {
  switch (type) {
  case 0x01:
    return EOS_FS_TYPE_FAT12;
  case 0x04:
  case 0x06:
  case 0x0E:
    return EOS_FS_TYPE_FAT16;
  case 0x0B:
  case 0x0C:
    return EOS_FS_TYPE_FAT32;
  case 0x07:
    return EOS_FS_TYPE_EXFAT; // or NTFS — needs deeper check
  case 0x83:
    return EOS_FS_TYPE_EXT;
  case 0x00:
    return EOS_FS_TYPE_UNKNOWN;
  default:
    return EOS_FS_TYPE_UNKNOWN;
  }
}

// ── Internal: MBR parsing ─────────────────────────────────────

static eos_error_t parse_mbr(eos_diskpart_t *dp) {
  mbr_t mbr;
  eos_error_t err = read_sector(dp->dev, 0, &mbr, 1);
  if (err != EOS_ERR_NO_ERROR)
    return err;

  dp->count = 0;
  for (int i = 0; i < 4; i++) {
    mbr_entry_t *e = &mbr.entries[i];
    if (e->type == 0x00 || e->lba_size == 0)
      continue;
    if (dp->count >= EOS_MAX_PARTITIONS)
      break;

    eos_part_t *p = &dp->parts[dp->count++];
    p->lba_start = e->lba_start;
    p->lba_size = e->lba_size;
    p->type_code = e->type;
    p->fs_type = type_code_to_fs(e->type);
    p->bootable = (e->status == 0x80);
  }

  return EOS_ERR_NO_ERROR;
}

// ── Internal: GPT parsing ─────────────────────────────────────

static eos_error_t parse_gpt(eos_diskpart_t *dp) {
  uint8_t sector[SECTOR_SIZE];
  eos_error_t err = read_sector(dp->dev, GPT_HEADER_LBA, sector, 1);
  if (err != EOS_ERR_NO_ERROR)
    return err;

  gpt_header_t *hdr = (gpt_header_t *)sector;
  if (hdr->signature != GPT_SIGNATURE)
    return EOS_ERR_DEVICE_INVALID;

  dp->count = 0;
  uint32_t max = hdr->num_part_entries < EOS_MAX_PARTITIONS
                     ? hdr->num_part_entries
                     : EOS_MAX_PARTITIONS;

  for (uint32_t i = 0; i < max; i++) {
    uint32_t entries_per_sector = SECTOR_SIZE / hdr->part_entry_size;
    uint32_t lba = (uint32_t)hdr->part_entry_lba + i / entries_per_sector;
    uint32_t offset = (i % entries_per_sector) * hdr->part_entry_size;

    uint8_t entry_sector[SECTOR_SIZE];
    err = read_sector(dp->dev, lba, entry_sector, 1);
    if (err != EOS_ERR_NO_ERROR)
      return err;

    gpt_entry_t *e = (gpt_entry_t *)(entry_sector + offset);

    // skip empty entries
    if (memcmp(e->type_guid, GPT_UNUSED_GUID, 16) == 0)
      continue;

    eos_part_t *p = &dp->parts[dp->count++];
    p->lba_start = (uint32_t)e->first_lba;
    p->lba_size = (uint32_t)(e->last_lba - e->first_lba + 1);
    p->type_code = 0;                 // GPT doesn't use MBR type codes
    p->fs_type = EOS_FS_TYPE_UNKNOWN; // needs deeper detection
    p->bootable = (e->attributes & 0x04) != 0;
  }

  return EOS_ERR_NO_ERROR;
}

// ── Internal: detection ───────────────────────────────────────

static eos_part_scheme_t detect_scheme(eos_dev_t *dev) {
  uint8_t sector[SECTOR_SIZE];
  if (read_sector(dev, 0, sector, 1) != EOS_ERR_NO_ERROR)
    return EOS_PART_SCHEME_UNKNOWN;

  uint16_t sig = *(uint16_t *)(sector + MBR_SIG_OFFSET);
  if (sig != MBR_SIGNATURE)
    return EOS_PART_SCHEME_RAW;

  // GPT protective MBR — first partition type is 0xEE
  mbr_t *mbr = (mbr_t *)sector;
  if (mbr->entries[0].type == GPT_PROTECTIVE)
    return EOS_PART_SCHEME_GPT;

  return EOS_PART_SCHEME_MBR;
}

// ── Public API ────────────────────────────────────────────────

eos_error_t eos_diskpart_open(eos_dev_t *dev, eos_diskpart_t **out) {
  if (!dev || !out)
    return EOS_ERR_DEVICE_INVALID;

  eos_diskpart_t *dp = calloc(1, sizeof(eos_diskpart_t));
  if (!dp)
    return EOS_ERR_NO_MEM_LEFT_ERROR;

  dp->dev = dev;
  dp->scheme = detect_scheme(dev);

  EOS_LOGI("diskpart: scheme = %s", EOS_PARTSCHEME_CSTR[dp->scheme]);

  eos_error_t err = EOS_ERR_NO_ERROR;
  switch (dp->scheme) {
  case EOS_PART_SCHEME_MBR:
    err = parse_mbr(dp);
    break;
  case EOS_PART_SCHEME_GPT:
    err = parse_gpt(dp);
    break;
  case EOS_PART_SCHEME_RAW:
    // whole device is one partition
    dp->count = 1;
    dp->parts[0].lba_start = 0;
    dp->parts[0].lba_size = UINT32_MAX; // unknown size
    dp->parts[0].fs_type = EOS_FS_TYPE_UNKNOWN;
    dp->parts[0].bootable = false;
    break;
  default:
    free(dp);
    return EOS_ERR_DEVICE_INVALID;
  }

  if (err != EOS_ERR_NO_ERROR) {
    free(dp);
    return err;
  }

  EOS_LOGI("diskpart: found %lu partition(s)", (unsigned long)dp->count);
  *out = dp;
  return EOS_ERR_NO_ERROR;
}

uint32_t eos_diskpart_count(eos_diskpart_t *dp) {
  if (!dp)
    return 0;
  return dp->count;
}

eos_error_t eos_diskpart_get(eos_diskpart_t *dp, uint32_t idx,
                             eos_part_t *out) {
  if (!dp || !out)
    return EOS_ERR_DEVICE_INVALID;
  if (idx >= dp->count)
    return EOS_ERR_CAP_NO_INVALID;
  *out = dp->parts[idx];
  return EOS_ERR_NO_ERROR;
}

void eos_diskpart_close(eos_diskpart_t *dp) { free(dp); }

// ── Partition management ──────────────────────────────────────

eos_error_t eos_diskpart_create(eos_dev_t *dev, eos_part_scheme_t scheme,
                                eos_diskpart_t **out) {
  if (!dev || !out)
    return EOS_ERR_DEVICE_INVALID;
  if (scheme != EOS_PART_SCHEME_MBR && scheme != EOS_PART_SCHEME_GPT)
    return EOS_ERR_DEVICE_INVALID;

  eos_diskpart_t *dp = calloc(1, sizeof(eos_diskpart_t));
  if (!dp)
    return EOS_ERR_NO_MEM_LEFT_ERROR;

  dp->dev = dev;
  dp->scheme = scheme;
  dp->count = 0;

  *out = dp;
  return EOS_ERR_NO_ERROR;
}

eos_error_t eos_diskpart_add(eos_diskpart_t *dp, uint32_t lba_start,
                             uint32_t lba_size, eos_fs_type_t fs_type) {
  if (!dp)
    return EOS_ERR_DEVICE_INVALID;
  if (dp->count >= EOS_MAX_PARTITIONS)
    return EOS_ERR_CAP_COUNT_QUOTA_EXCEED;

  // check overlap with existing partitions
  for (uint32_t i = 0; i < dp->count; i++) {
    uint32_t s = dp->parts[i].lba_start;
    uint32_t e = s + dp->parts[i].lba_size;
    uint32_t ns = lba_start;
    uint32_t ne = lba_start + lba_size;
    if (ns < e && ne > s)
      return EOS_ERR_DEVICE_ALREADY_ATTACHED; // overlap
  }

  eos_part_t *p = &dp->parts[dp->count++];
  p->lba_start = lba_start;
  p->lba_size = lba_size;
  p->fs_type = fs_type;
  p->bootable = false;

  // map fs_type back to type_code for MBR
  switch (fs_type) {
  case EOS_FS_TYPE_FAT12:
    p->type_code = 0x01;
    break;
  case EOS_FS_TYPE_FAT16:
    p->type_code = 0x06;
    break;
  case EOS_FS_TYPE_FAT32:
    p->type_code = 0x0B;
    break;
  case EOS_FS_TYPE_EXFAT:
    p->type_code = 0x07;
    break;
  case EOS_FS_TYPE_EXT:
    p->type_code = 0x83;
    break;
  default:
    p->type_code = 0x00;
    break;
  }

  return EOS_ERR_NO_ERROR;
}

eos_error_t eos_diskpart_remove(eos_diskpart_t *dp, uint32_t idx) {
  if (!dp)
    return EOS_ERR_DEVICE_INVALID;
  if (idx >= dp->count)
    return EOS_ERR_CAP_NO_INVALID;

  // shift remaining partitions down
  for (uint32_t i = idx; i < dp->count - 1; i++)
    dp->parts[i] = dp->parts[i + 1];

  memset(&dp->parts[--dp->count], 0, sizeof(eos_part_t));
  return EOS_ERR_NO_ERROR;
}

eos_error_t eos_diskpart_commit(eos_diskpart_t *dp) {
  if (!dp)
    return EOS_ERR_DEVICE_INVALID;
  if (dp->scheme != EOS_PART_SCHEME_MBR)
    return EOS_ERR_DEVICE_INVALID; // GPT commit TODO

  // build MBR sector
  uint8_t sector[SECTOR_SIZE];
  memset(sector, 0, sizeof(sector));

  mbr_t *mbr = (mbr_t *)sector;
  *(uint16_t *)(sector + MBR_SIG_OFFSET) = MBR_SIGNATURE;

  for (uint32_t i = 0; i < dp->count && i < 4; i++) {
    mbr->entries[i].status = dp->parts[i].bootable ? 0x80 : 0x00;
    mbr->entries[i].type = dp->parts[i].type_code;
    mbr->entries[i].lba_start = dp->parts[i].lba_start;
    mbr->entries[i].lba_size = dp->parts[i].lba_size;
    // CHS fields left as zero — LBA mode used universally
  }

  return write_sector(dp->dev, 0, sector, 1);
}

eos_error_t eos_diskpart_free_space(eos_diskpart_t *dp, uint32_t *lba_start_out,
                                    uint32_t *lba_size_out) {
  if (!dp || !lba_start_out || !lba_size_out)
    return EOS_ERR_DEVICE_INVALID;

  // first usable LBA — skip MBR + some alignment
  uint32_t first_usable = 2048; // standard alignment (1MB)
  uint32_t last_usable = UINT32_MAX;

  // find highest end of existing partitions
  uint32_t highest_end = first_usable;
  for (uint32_t i = 0; i < dp->count; i++) {
    uint32_t end = dp->parts[i].lba_start + dp->parts[i].lba_size;
    if (end > highest_end)
      highest_end = end;
  }

  if (highest_end >= last_usable) {
    *lba_start_out = 0;
    *lba_size_out = 0;
    return EOS_ERR_NO_ERROR; // no free space
  }

  *lba_start_out = highest_end;
  *lba_size_out = last_usable - highest_end;
  return EOS_ERR_NO_ERROR;
}