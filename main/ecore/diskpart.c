#include "diskpart.h"
#include "ecore/device.h"
#include "ecore/driver.h"
#include <stddef.h>
#include <stdint.h>

#define MBR_LEN 512
#define MBR_SIGNATURE_OFFSET 510
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_PARTITION_TYPE_OFFSET 4

#define GPT_PROTECTIVE_PARTITION_TYPE 0xEE

const char EOS_PARTSCHEME_CSTR[][8] = {"RAW", "MBR", "GPT", "UNKNOWN"};

eos_part_scheme_t eos_diskpart_detect(eos_dev_t *dev) {
  uint8_t header[MBR_LEN];
  // Jump to header
  dev->driver->lseek(dev, SEEK_SET, 0);

  if (dev->driver->read(dev, header, MBR_LEN) != MBR_LEN)
    return EOS_PART_SCHEME_UNKNOWN;

  // Check for valid MBR signature
  if (header[MBR_SIGNATURE_OFFSET] != 0x55 ||
      header[MBR_SIGNATURE_OFFSET + 1] != 0xAA) {
    return EOS_PART_SCHEME_RAW;
  }

  // Inspect the four MBR partition entries
  for (int i = 0; i < 4; i++) {
    const uint8_t *entry =
        &header[MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE];

    uint8_t type = entry[MBR_PARTITION_TYPE_OFFSET];

    // Protective MBR used by GPT
    if (type == GPT_PROTECTIVE_PARTITION_TYPE) {
      return EOS_PART_SCHEME_GPT;
    }
  }

  // Valid MBR with normal partition entries
  return EOS_PART_SCHEME_MBR;
}