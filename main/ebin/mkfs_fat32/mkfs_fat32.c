#include "mkfs_fat32.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFU);
  p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void put_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFU);
  p[1] = (uint8_t)((v >> 8) & 0xFFU);
  p[2] = (uint8_t)((v >> 16) & 0xFFU);
  p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static int write_sectors_at(int fd, uint32_t lba, const void *buf, size_t len) {
  if (lseek(fd, (off_t)lba, SEEK_SET) != (off_t)lba) {
    return -1;
  }
  const uint8_t *p = (const uint8_t *)buf;
  size_t rem = len;
  while (rem > 0) {
    ssize_t w = write(fd, p, rem);
    if (w <= 0) {
      return -1;
    }
    p += (size_t)w;
    rem -= (size_t)w;
  }
  return 0;
}

static int zero_sectors(int fd, uint32_t start_lba, uint32_t count,
                        uint32_t sec_sz, uint8_t *zero_buf, uint32_t chunk_secs) {
  uint32_t lba = start_lba;
  uint32_t rem = count;
  while (rem > 0) {
    uint32_t n = (rem > chunk_secs) ? chunk_secs : rem;
    if (write_sectors_at(fd, lba, zero_buf, (size_t)n * sec_sz) < 0) {
      return -1;
    }
    lba += n;
    rem -= n;
  }
  return 0;
}

static uint32_t pick_spc(uint32_t total_sectors) {
  uint32_t spc = 1;
  if (total_sectors > 33554432U) {
    spc = 32;
  } else if (total_sectors > 16777216U) {
    spc = 16;
  } else if (total_sectors > 532480U) {
    spc = 8;
  }
  while (spc > 1 && (total_sectors / spc) < EOS_MKFS_FAT32_MIN_CLUSTERS) {
    spc /= 2;
  }
  return spc;
}

static void build_bpb_sector(uint8_t *sec, uint32_t sec_sz, uint32_t total_secs,
                             uint32_t spc, uint32_t fat_sz, const char *label) {
  memset(sec, 0, sec_sz);
  sec[0] = 0xEBU;
  sec[1] = 0x58U;
  sec[2] = 0x90U;
  memcpy(sec + 3, "EOSFAT32", 8);
  put_le16(sec + 11, (uint16_t)sec_sz);
  sec[13] = (uint8_t)spc;
  put_le16(sec + 14, EOS_MKFS_FAT32_RSVD_SECS);
  sec[16] = EOS_MKFS_FAT32_NUM_FATS;
  sec[21] = EOS_MKFS_FAT32_MEDIA_FIXED;
  put_le16(sec + 24, EOS_MKFS_FAT32_SECS_PER_TRK);
  put_le16(sec + 26, EOS_MKFS_FAT32_NUM_HEADS);
  put_le32(sec + 32, total_secs);
  put_le32(sec + 36, fat_sz);
  put_le32(sec + 44, 2);
  put_le16(sec + EOS_MKFS_FAT32_OFF_FSINFO, EOS_MKFS_FAT32_FSINFO_SEC);
  put_le16(sec + EOS_MKFS_FAT32_OFF_BKBOOT, EOS_MKFS_FAT32_BACKUP_BOOT_SEC);
  sec[EOS_MKFS_FAT32_OFF_DRVNUM] = EOS_MKFS_FAT32_DRV_NUM_HDD;
  sec[66] = EOS_MKFS_FAT32_EXT_BOOT_SIG;
  put_le32(sec + 67, EOS_MKFS_FAT32_VOL_ID);

  memset(sec + 71, ' ', EOS_MKFS_FAT32_VOL_LABEL_LEN);
  if (label && label[0] != '\0') {
    size_t n = strlen(label);
    if (n > EOS_MKFS_FAT32_VOL_LABEL_LEN) {
      n = EOS_MKFS_FAT32_VOL_LABEL_LEN;
    }
    memcpy(sec + 71, label, n);
  } else {
    memcpy(sec + 71, "EOS SD     ", EOS_MKFS_FAT32_VOL_LABEL_LEN);
  }
  memcpy(sec + 82, "FAT32   ", 8);
  sec[EOS_MKFS_FAT32_SIG_OFFSET] = EOS_MKFS_FAT32_SIG_0;
  sec[EOS_MKFS_FAT32_SIG_OFFSET + 1] = EOS_MKFS_FAT32_SIG_1;
}

static void build_fsinfo_sector(uint8_t *sec, uint32_t sec_sz, uint32_t free_clusters) {
  memset(sec, 0, sec_sz);
  put_le32(sec + 0, 0x41615252U);
  put_le32(sec + 484, 0x61417272U);
  put_le32(sec + 488, free_clusters);
  put_le32(sec + 492, 3U);
  put_le32(sec + 508, 0xAA550000U);
}

static int format_fat32_device(int fd, const char *dev_path, const char *label, uint32_t spc_arg) {
  bool umount_ok = false;
  (void)ioctl(fd, EOS_STORAGE_IOCTL_UMOUNT, &umount_ok);

  uint32_t sec_sz = EOS_MKFS_FAT32_SECTOR_SZ;
  uint32_t total_secs = 0;
  (void)ioctl(fd, EOS_STORAGE_IOCTL_GET_SECTOR_SIZE, &sec_sz);
  if (sec_sz == 0) {
    sec_sz = EOS_MKFS_FAT32_SECTOR_SZ;
  }
  if (ioctl(fd, EOS_STORAGE_IOCTL_GET_CAPACITY, &total_secs) != 0 || total_secs == 0) {
    off_t end = lseek(fd, 0, SEEK_END);
    if (end > 0) {
      total_secs = (uint32_t)end;
    }
  }
  if (total_secs <= EOS_MKFS_FAT32_RSVD_SECS) {
    fprintf(stderr, "mkfs.fat32: %s: device too small (%lu sectors)\n", dev_path, (unsigned long)total_secs);
    return 1;
  }

  uint32_t spc = spc_arg ? spc_arg : pick_spc(total_secs);
  uint32_t entries_per_sec = sec_sz / 4U;
  uint32_t clusters_est = (total_secs - EOS_MKFS_FAT32_RSVD_SECS) / spc;
  uint32_t fat_sz = (clusters_est + 2U + entries_per_sec - 1U) / entries_per_sec;
  uint32_t data_secs = total_secs - EOS_MKFS_FAT32_RSVD_SECS - EOS_MKFS_FAT32_NUM_FATS * fat_sz;
  uint32_t total_clusters = data_secs / spc;
  if (total_clusters < EOS_MKFS_FAT32_MIN_CLUSTERS) {
    fprintf(stderr, "mkfs.fat32: %s: only %lu clusters (need >= %u for FAT32)\n",
            dev_path, (unsigned long)total_clusters, EOS_MKFS_FAT32_MIN_CLUSTERS);
    return 1;
  }

  uint8_t *chunk = (uint8_t *)calloc(EOS_MKFS_FAT32_ZERO_CHUNK_SECS, sec_sz);
  uint8_t *sec = (uint8_t *)calloc(1, sec_sz);
  if (!chunk || !sec) {
    free(chunk);
    free(sec);
    fprintf(stderr, "mkfs.fat32: out of memory\n");
    return 1;
  }

  int rc = 0;
  if (zero_sectors(fd, 0, EOS_MKFS_FAT32_RSVD_SECS, sec_sz, chunk, EOS_MKFS_FAT32_ZERO_CHUNK_SECS) < 0) {
    rc = -1;
  }
  if (rc == 0) {
    build_bpb_sector(sec, sec_sz, total_secs, spc, fat_sz, label);
    if (write_sectors_at(fd, 0, sec, sec_sz) < 0 ||
        write_sectors_at(fd, EOS_MKFS_FAT32_BACKUP_BOOT_SEC, sec, sec_sz) < 0) {
      rc = -1;
    }
  }
  if (rc == 0) {
    build_fsinfo_sector(sec, sec_sz, total_clusters - 1U);
    if (write_sectors_at(fd, EOS_MKFS_FAT32_FSINFO_SEC, sec, sec_sz) < 0 ||
        write_sectors_at(fd, EOS_MKFS_FAT32_BACKUP_FSINFO, sec, sec_sz) < 0) {
      rc = -1;
    }
  }
  if (rc == 0) {
    uint32_t fat1_lba = EOS_MKFS_FAT32_RSVD_SECS;
    uint32_t fat2_lba = fat1_lba + fat_sz;
    if (zero_sectors(fd, fat1_lba, EOS_MKFS_FAT32_NUM_FATS * fat_sz,
                     sec_sz, chunk, EOS_MKFS_FAT32_ZERO_CHUNK_SECS) < 0) {
      rc = -1;
    } else {
      memset(sec, 0, sec_sz);
      put_le32(sec + 0, 0x0FFFFFF8U);
      put_le32(sec + 4, 0xFFFFFFFFU);
      put_le32(sec + 8, 0x0FFFFFFFU);
      if (write_sectors_at(fd, fat1_lba, sec, sec_sz) < 0 ||
          write_sectors_at(fd, fat2_lba, sec, sec_sz) < 0) {
        rc = -1;
      }
    }
  }
  if (rc == 0) {
    uint32_t root_lba = EOS_MKFS_FAT32_RSVD_SECS + EOS_MKFS_FAT32_NUM_FATS * fat_sz;
    if (zero_sectors(fd, root_lba, spc, sec_sz, chunk, EOS_MKFS_FAT32_ZERO_CHUNK_SECS) < 0) {
      rc = -1;
    }
  }

  free(chunk);
  free(sec);
  if (rc < 0) {
    fprintf(stderr, "mkfs.fat32: write failed on %s: %s\n", dev_path, strerror(errno));
    return 1;
  }
  printf("mkfs.fat32: formatted %s (%lu sectors, spc=%lu, fat_sz=%lu, clusters=%lu)\n",
         dev_path, (unsigned long)total_secs, (unsigned long)spc,
         (unsigned long)fat_sz, (unsigned long)total_clusters);
  return 0;
}

static void mkfs_print_usage(void) {
  printf("Usage: mkfs.fat32 [-L label] [-c spc] <device>\n");
}

int mkfs_fat32_main(int argc, char **argv) {
  const char *dev_path = NULL;
  const char *label = NULL;
  uint32_t spc_arg = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      mkfs_print_usage();
      return 0;
    } else if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
      label = argv[++i];
    } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
      spc_arg = (uint32_t)strtoul(argv[++i], NULL, 10);
    } else if (!dev_path) {
      dev_path = argv[i];
    } else {
      mkfs_print_usage();
      return 1;
    }
  }

  if (!dev_path) {
    mkfs_print_usage();
    return 1;
  }

  int fd = open(dev_path, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "mkfs.fat32: %s: %s\n", dev_path, strerror(errno));
    return 1;
  }
  int rc = format_fat32_device(fd, dev_path, label, spc_arg);
  close(fd);
  return rc;
}

EOS_BIN_ATTR const eos_bin_t mkfs_fat32_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "mkfs.fat32",
    .name = "Format FAT32",
    .group = "System",
    .description = "Format a block device or partition as FAT32",
    .entry_point = mkfs_fat32_main,
};
