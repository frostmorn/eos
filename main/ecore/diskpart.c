#include "diskpart.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "emisc/fancymacro.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

const char *eos_part_scheme_str(eos_part_scheme_t scheme) {
  switch (scheme) {
  case EOS_PART_SCHEME_RAW:
    return "RAW";
  case EOS_PART_SCHEME_MBR:
    return "MBR";
  case EOS_PART_SCHEME_GPT:
    return "GPT";

  default:
    return "UNKNOWN";
  }
}

// ── MBR structures ────────────────────────────────────────────

#define MBR_SIGNATURE 0xAA55
#define MBR_SIG_OFFSET 510
#define GPT_PROTECTIVE 0xEE
#define SECTOR_SIZE 512

typedef struct __attribute__((packed)) {
  uint8_t status;
  uint8_t chs_first[3];
  uint8_t part_type;
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

const char *eos_part_type_str(eos_part_type_t type) {
  switch (type) {
  case EOS_PART_EMPTY:
    return "Empty";
  case EOS_PART_FAT12:
    return "FAT12";
  case EOS_PART_FAT16_LT32MB:
    return "FAT16 <32MB";
  case EOS_PART_FAT16B:
    return "FAT16B";
  case EOS_PART_FAT32:
    return "FAT32";
  case EOS_PART_FAT32_LBA:
    return "FAT32 LBA";
  case EOS_PART_FAT16_LBA:
    return "FAT16 LBA";
  case EOS_PART_FAT12_HIDDEN:
    return "FAT12 Hidden";
  case EOS_PART_FAT16_HIDDEN:
    return "FAT16 Hidden";
  case EOS_PART_FAT16B_HIDDEN:
    return "FAT16B Hidden";
  case EOS_PART_FAT32_HIDDEN:
    return "FAT32 Hidden";
  case EOS_PART_FAT32_LBA_HIDDEN:
    return "FAT32 LBA Hidden";
  case EOS_PART_FAT16_LBA_HIDDEN:
    return "FAT16 LBA Hidden";
  case EOS_PART_EXTENDED_CHS:
    return "Extended CHS";
  case EOS_PART_EXTENDED_LBA:
    return "Extended LBA";
  case EOS_PART_EXTENDED_HIDDEN:
    return "Extended Hidden";
  case EOS_PART_EXTENDED_LBA2:
    return "Extended LBA Hidden";
  case EOS_PART_NTFS_EXFAT:
    return "NTFS/exFAT";
  case EOS_PART_NTFS_HIDDEN:
    return "NTFS Hidden";
  case EOS_PART_WINRE:
    return "Windows RE";
  case EOS_PART_LINUX_SWAP:
    return "Linux Swap";
  case EOS_PART_LINUX_FS:
    return "Linux FS";
  case EOS_PART_LINUX_EXTENDED:
    return "Linux Extended";
  case EOS_PART_LINUX_LVM:
    return "Linux LVM";
  case EOS_PART_LINUX_RAID:
    return "Linux RAID";
  case EOS_PART_LINUX_FS_HIDDEN:
    return "Linux FS Hidden";
  case EOS_PART_FREEBSD:
    return "FreeBSD";
  case EOS_PART_OPENBSD:
    return "OpenBSD";
  case EOS_PART_NETBSD:
    return "NetBSD";
  case EOS_PART_DRAGONFLY:
    return "DragonFly BSD";
  case EOS_PART_MACOS_X:
    return "macOS X";
  case EOS_PART_MACOS_X_BOOT:
    return "macOS X Boot";
  case EOS_PART_MACOS_X_HFS:
    return "macOS X HFS+";
  case EOS_PART_WIN_DYNAMIC:
    return "Windows Dynamic";
  case EOS_PART_WIN_LDM_META:
    return "Windows LDM Meta";
  case EOS_PART_WIN_LDM_DATA:
    return "Windows LDM Data";
  case EOS_PART_WIN_RECOVERY:
    return "Windows Recovery";
  case EOS_PART_IBM_RECOVERY:
    return "IBM/Compaq Diag";
  case EOS_PART_GPT_PROTECTIVE:
    return "GPT Protective";
  case EOS_PART_EFI_SYSTEM:
    return "EFI System";
  case EOS_PART_SOLARIS_X86:
    return "Solaris x86";
  case EOS_PART_SOLARIS_BOOT:
    return "Solaris Boot";
  case EOS_PART_UNIX_SYS_V:
    return "Unix System V";
  case EOS_PART_QNX4_P1:
    return "QNX4 P1";
  case EOS_PART_QNX4_P2:
    return "QNX4 P2";
  case EOS_PART_QNX4_P3:
    return "QNX4 P3";
  case EOS_PART_OS2_BOOT_MGR:
    return "OS/2 Boot Mgr";
  case EOS_PART_INTEL_RST:
    return "Intel RST/Hibernate";
  case EOS_PART_LUKS:
    return "Linux LUKS";
  case EOS_PART_VMWARE_VMFS:
    return "VMware VMFS";
  case EOS_PART_VMWARE_SWAP:
    return "VMware Swap";
  case EOS_PART_XENIX_ROOT:
    return "XENIX Root";
  case EOS_PART_XENIX_USR:
    return "XENIX Usr";
  case EOS_PART_PLAN9:
    return "Plan 9";
  case EOS_PART_WHOLE_DISK:
    return "Whole Disk";
  case EOS_PART_GPT_ENTRY:
    return "GPT Entry";
  default:
    return "Unknown";
  }
}

// ── Internal: sector I/O ──────────────────────────────────────

static int read_sector(eos_dev_t *dev, uint32_t lba, void *buf,
                       uint32_t count) {
  off_t offset = (off_t)lba * SECTOR_SIZE;
  size_t bytes = (size_t)count * SECTOR_SIZE;
  EOS_LOGI("diskpart: READ lba=%lu count=%lu offset=%lld bytes=%lu",
           (unsigned long)lba, (unsigned long)count, (long long)offset,
           (unsigned long)bytes);

  if (eos_drv_lseek(dev, EOS_DRV_FD, offset, SEEK_SET) != offset) {
    EOS_LOGI("diskpart: READ seek FAILED offset=%lld", (long long)offset);
    errno = ENODEV;
    return -1;
  }
  if (eos_drv_read(dev, EOS_DRV_FD, buf, bytes) != (int)bytes) {
    EOS_LOGI("diskpart: READ FAILED lba=%lu offset=%lld bytes=%lu",
             (unsigned long)lba, (long long)offset, (unsigned long)bytes);
    errno = EIO;
    return -1;
  }
  return 0;
}

static int write_sector(eos_dev_t *dev, uint32_t lba, const void *buf,
                        uint32_t count) {
  off_t offset = (off_t)lba * SECTOR_SIZE;
  size_t bytes = (size_t)count * SECTOR_SIZE;
  EOS_LOGI("diskpart: WRITE lba=%lu count=%lu offset=%lld bytes=%lu",
           (unsigned long)lba, (unsigned long)count, (long long)offset,
           (unsigned long)bytes);
  if (eos_drv_lseek(dev, EOS_DRV_FD, offset, SEEK_SET) != offset) {
    EOS_LOGI("diskpart: WRITE seek FAILED offset=%lld", (long long)offset);
    errno = ENODEV;
    return -1;
  }
  if (eos_drv_write(dev, EOS_DRV_FD, (void *)buf, bytes) != (int)bytes) {
    EOS_LOGI("diskpart: WRITE FAILED lba=%lu offset=%lld bytes=%lu",
             (unsigned long)lba, (long long)offset, (unsigned long)bytes);
    errno = EIO;
    return -1;
  }
  return 0;
}

// ── Internal: MBR parsing ─────────────────────────────────────

static int parse_mbr(eos_part_table_t *dp) {
  mbr_t mbr;
  int err = read_sector(dp->dev, 0, &mbr, 1);
  if (err != 0)
    return err;

  dp->count = 0;
  for (int i = 0; i < 4; i++) {
    mbr_entry_t *e = &mbr.entries[i];
    if (e->part_type == 0x00 || e->lba_size == 0)
      continue;
    if (dp->count >= EOS_MAX_PARTITIONS)
      break;

    eos_part_info_t *p = &dp->parts[dp->count++];
    p->lba_start = e->lba_start;
    p->lba_size = e->lba_size;
    p->part_type = e->part_type;
    p->bootable = (e->status == 0x80);
  }

  return 0;
}

static eos_part_type_t gpt_guid_to_part_type(const uint8_t guid[16]) {
  // Unused entry
  if (memcmp(guid, (const uint8_t[16]){0}, 16) == 0)
    return EOS_PART_EMPTY;

  // EFI System
  if (memcmp(guid,
             (const uint8_t[16]){0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9,
                                 0x3B},
             16) == 0)
    return EOS_PART_EFI_SYSTEM;

  // Microsoft Basic Data (NTFS/FAT/exFAT)
  if (memcmp(guid,
             (const uint8_t[16]){0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                                 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99,
                                 0xC7},
             16) == 0)
    return EOS_PART_NTFS_EXFAT;

  // Linux filesystem
  if (memcmp(guid,
             (const uint8_t[16]){0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
                                 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D,
                                 0xE4},
             16) == 0)
    return EOS_PART_LINUX_FS;

  // Linux swap
  if (memcmp(guid,
             (const uint8_t[16]){0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43,
                                 0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F,
                                 0x4F},
             16) == 0)
    return EOS_PART_LINUX_SWAP;

  // Linux LVM
  if (memcmp(guid,
             (const uint8_t[16]){0x79, 0xD3, 0xD6, 0xE6, 0x07, 0xF5, 0xC2, 0x44,
                                 0xA2, 0x3C, 0x23, 0x8F, 0x2A, 0x3D, 0xF9,
                                 0x28},
             16) == 0)
    return EOS_PART_LINUX_LVM;

  // Linux RAID
  if (memcmp(guid,
             (const uint8_t[16]){0x0F, 0x88, 0x9D, 0xA1, 0xFC, 0x05, 0x3B, 0x4D,
                                 0xA0, 0x06, 0x74, 0x3F, 0x0F, 0x84, 0x91,
                                 0x1E},
             16) == 0)
    return EOS_PART_LINUX_RAID;

  // Linux LUKS
  if (memcmp(guid,
             (const uint8_t[16]){0xCB, 0x7C, 0x7D, 0xCA, 0xED, 0x63, 0x53, 0x4C,
                                 0x86, 0x1C, 0x17, 0x42, 0x53, 0x60, 0x59,
                                 0xCC},
             16) == 0)
    return EOS_PART_LUKS;

  // Apple HFS+
  if (memcmp(guid,
             (const uint8_t[16]){0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xAA, 0x11,
                                 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC,
                                 0xAC},
             16) == 0)
    return EOS_PART_MACOS_X_HFS;

  // Apple APFS
  if (memcmp(guid,
             (const uint8_t[16]){0xEF, 0x57, 0x34, 0x7C, 0x00, 0x00, 0xAA, 0x11,
                                 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC,
                                 0xAC},
             16) == 0)
    return EOS_PART_MACOS_X;

  // Windows Recovery
  if (memcmp(guid,
             (const uint8_t[16]){0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D,
                                 0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6,
                                 0xAC},
             16) == 0)
    return EOS_PART_WIN_RECOVERY;

  // Unknown GPT type
  return EOS_PART_GPT_ENTRY;
}

// ── Internal: GPT parsing ─────────────────────────────────────

static int parse_gpt(eos_part_table_t *dp) {
  uint8_t sector[SECTOR_SIZE];
  int err = read_sector(dp->dev, GPT_HEADER_LBA, sector, 1);
  if (err != 0)
    return err;

  gpt_header_t *hdr = (gpt_header_t *)sector;
  if (hdr->signature != GPT_SIGNATURE) {
    errno = ENODEV;
    return -1;
  }

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
    if (err != 0)
      return err;
    gpt_entry_t *e = (gpt_entry_t *)(entry_sector + offset);

    // skip empty entries
    if (memcmp(e->type_guid, GPT_UNUSED_GUID, 16) == 0)
      continue;

    eos_part_info_t *p = &dp->parts[dp->count++];
    p->lba_start = (uint32_t)e->first_lba;
    p->lba_size = (uint32_t)(e->last_lba - e->first_lba + 1);
    p->part_type = gpt_guid_to_part_type(e->type_guid);
    p->bootable = (e->attributes & 0x04) != 0;
  }

  return 0;
}

// ── Internal: detection ───────────────────────────────────────

static eos_part_scheme_t detect_scheme(eos_dev_t *dev) {
  uint8_t sector[SECTOR_SIZE];
  if (read_sector(dev, 0, sector, 1) != 0)
    return EOS_PART_SCHEME_UNKNOWN;

  // Endianess?
  uint16_t sig = *(uint16_t *)(sector + MBR_SIG_OFFSET);
  if (sig != MBR_SIGNATURE)
    return EOS_PART_SCHEME_RAW;

  // GPT protective MBR — first partition type is 0xEE
  mbr_t *mbr = (mbr_t *)sector;
  if (mbr->entries[0].part_type == GPT_PROTECTIVE)
    return EOS_PART_SCHEME_GPT;

  return EOS_PART_SCHEME_MBR;
}

// ── Public API ────────────────────────────────────────────────

int eos_diskpart_parse(eos_dev_t *dev, eos_part_table_t *out) {
  out->dev = dev;
  out->scheme = detect_scheme(dev);

  EOS_LOGI("diskpart: scheme = %s", eos_part_scheme_str(out->scheme));

  int err = 0;

  switch (out->scheme) {
  case EOS_PART_SCHEME_MBR:
    err = parse_mbr(out);
    break;
  case EOS_PART_SCHEME_GPT:
    err = parse_gpt(out);
    break;
  case EOS_PART_SCHEME_RAW:
    // whole device is one partition
    out->count = 1;
    out->parts[0].lba_start = 0;
    out->parts[0].lba_size = UINT32_MAX; // unknown size
    out->parts[0].part_type = EOS_PART_EMPTY;
    out->parts[0].bootable = false;
    break;
  default:
    errno = ENODEV;
    return -1;
  }

  EOS_LOGI("diskpart: found %lu partition(s)", (unsigned long)out->count);

  for (uint32_t i = 0; i < out->count; i++){
    EOS_LOGI("diskpart: partition %d -> lba_start = %d; lba_size = %d", 
      (int)i, 
      (int)out->parts[i].lba_start, 
      (int)out->parts[i].lba_size
    );

    EOS_LOGI("bootable = %d; part_type=%d->%s", 
      out->parts[i].bootable, 
      out->parts[i].part_type,
      eos_part_type_str(out->parts[i].part_type)
    );
  }

  return err;
}

uint32_t eos_diskpart_count(eos_part_table_t *dp) {
  if (!dp)
    return 0;
  return dp->count;
}

int eos_diskpart_get(eos_part_table_t *dp, uint32_t idx,
                     eos_part_info_t *out) {
  if (!dp || !out) {
    errno = EINVAL;
    return -1;
  }
  if (idx >= dp->count) {
    errno = EINVAL;
    return -1;
  }
  *out = dp->parts[idx];
  return 0;
}

// ── Partition management ──────────────────────────────────────

int eos_diskpart_create(eos_dev_t *dev, eos_part_scheme_t scheme,
                        eos_part_table_t **out) {
  if (!dev || !out) {
    errno = EINVAL;
    return -1;
  }
  if (scheme != EOS_PART_SCHEME_MBR && scheme != EOS_PART_SCHEME_GPT) {
    errno = EINVAL;
    return -1;
  }

  eos_part_table_t *dp = calloc(1, sizeof(eos_part_table_t));
  if (!dp) {
    errno = ENOMEM;
    return -1;
  }

  dp->dev = dev;
  dp->scheme = scheme;
  dp->count = 0;

  *out = dp;
  return 0;
}

int eos_diskpart_add(eos_part_table_t *dp, uint32_t lba_start,
                     uint32_t lba_size, eos_part_type_t part_type) {
  if (!dp) {
    errno = EINVAL;
    return -1;
  }
  if (dp->count >= EOS_MAX_PARTITIONS) {
    errno = ENOSPC;
    return -1;
  }

  // check overlap with existing partitions
  for (uint32_t i = 0; i < dp->count; i++) {
    uint32_t s = dp->parts[i].lba_start;
    uint32_t e = s + dp->parts[i].lba_size;
    uint32_t ns = lba_start;
    uint32_t ne = lba_start + lba_size;
    if (ns < e && ne > s) {
      errno = EEXIST; // overlap
      return -1;
    }
  }

  eos_part_info_t *p = &dp->parts[dp->count++];
  p->lba_start = lba_start;
  p->lba_size = lba_size;
  p->part_type = part_type;
  p->bootable = false;

  return 0;
}

int eos_diskpart_remove(eos_part_table_t *dp, uint32_t idx) {
  if (!dp) {
    errno = EINVAL;
    return -1;
  }
  if (idx >= dp->count) {
    errno = EINVAL;
    return -1;
  }

  // shift remaining partitions down
  for (uint32_t i = idx; i < dp->count - 1; i++)
    dp->parts[i] = dp->parts[i + 1];

  memset(&dp->parts[--dp->count], 0, sizeof(eos_part_info_t));
  return 0;
}

int eos_diskpart_commit(eos_part_table_t *dp) {
  if (!dp) {
    errno = EINVAL;
    return -1;
  }
  if (dp->scheme != EOS_PART_SCHEME_MBR) {
    errno = ENOTSUP; // GPT commit TODO
    return -1;
  }

  // build MBR sector
  uint8_t sector[SECTOR_SIZE];
  memset(sector, 0, sizeof(sector));

  mbr_t *mbr = (mbr_t *)sector;
  *(uint16_t *)(sector + MBR_SIG_OFFSET) = MBR_SIGNATURE;

  for (uint32_t i = 0; i < dp->count && i < 4; i++) {
    mbr->entries[i].status = dp->parts[i].bootable ? 0x80 : 0x00;
    mbr->entries[i].part_type = dp->parts[i].part_type;
    mbr->entries[i].lba_start = dp->parts[i].lba_start;
    mbr->entries[i].lba_size = dp->parts[i].lba_size;
    // CHS fields left as zero — LBA mode used universally
  }

  return write_sector(dp->dev, 0, sector, 1);
}

int eos_diskpart_free_space(eos_part_table_t *dp,
                            uint32_t *lba_start_out,
                            uint32_t *lba_size_out) {
  if (!dp || !lba_start_out || !lba_size_out) {
    errno = EINVAL;
    return -1;
  }

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
    return 0; // no free space
  }

  *lba_start_out = highest_end;
  *lba_size_out = last_usable - highest_end;
  return 0;
}

bool eos_diskpart_is_fat(eos_part_type_t type){
  switch(type){
    case EOS_PART_FAT12:; 
    case EOS_PART_FAT16_LT32MB:;
    case EOS_PART_FAT16B:;
    case EOS_PART_FAT32:;
    case EOS_PART_FAT32_LBA:; 
    case EOS_PART_FAT16_LBA:;
    case EOS_PART_FAT12_HIDDEN:;
    case EOS_PART_FAT16_HIDDEN:; 
    case EOS_PART_FAT16B_HIDDEN:;
    case EOS_PART_FAT32_HIDDEN:;
    case EOS_PART_FAT32_LBA_HIDDEN:;
    case EOS_PART_FAT16_LBA_HIDDEN: {
      return true;
    }
    default: return false;
  }
  return false;
}
