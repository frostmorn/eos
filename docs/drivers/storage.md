# Storage Driver Contracts & Block Architecture

Storage drivers in EOS reside under `scope == "storage"`, providing block-level read and write access to flash memory and SD/SDIO cards through standard Virtual Filesystem nodes (such as `/dev/sd0` and `/dev/flash0`).

---

## 1. Supported Storage Drivers (`main/edriver/storage/`)

| Driver Name | Parent Bus | Typical Node | Supported Media & Protocols |
| :--- | :--- | :--- | :--- |
| **`sd`** | `spi0` or `sdio0` | `/dev/sd0` | MicroSD cards via SPI mode (Lilka v2.3) or 4-bit SDMMC bus (JC4880P4). |
| **`flash`** | `root` | `/dev/flash0` | Onboard SPI Flash partitions (FAT, LittleFS, raw storage). |

---

## 2. VFS Block I/O Contract

Storage devices accept standard POSIX file operations:
* **`open(path, O_RDWR)`**: Prepares block controller.
* **`read(fd, buffer, bytes)`**: Reads whole sectors or partial blocks. If unaligned, the driver utilizes an internal bounce buffer.
* **`write(fd, buffer, bytes)`**: Writes sectors to persistent storage media.
* **`lseek(fd, offset, whence)`**: Moves the current byte offset.

### 32-bit `off_t` Addressing Considerations:
Because ESP-IDF uses a 32-bit signed integer for `off_t`, the maximum direct byte seek offset is limited to **2 GB** ($2^{31}-1$ bytes). For storage cards exceeding 2 GB:
* Partition tables (`diskpart.c`) divide media into logical partitions within addressable limits.
* Applications query block geometry via `ioctl` rather than relying on byte-offset seeking.

---

## 3. Storage IOCTL Interface

All storage subsystem IOCTLs reside in range `150–199`:

| IOCTL Command | Code | Argument Type | Description |
| :--- | :---: | :--- | :--- |
| `EOS_STORAGE_IOCTL_GET_SECTOR_SIZE` | `150` | `uint32_t *out_sector_size` | Returns physical hardware sector size in bytes (typically 512). |
| `EOS_STORAGE_IOCTL_GET_CAPACITY` | `151` | `uint64_t *out_capacity_bytes`| Returns total media storage capacity in bytes (supports 64-bit sizes). |
| `EOS_STORAGE_IOCTL_MOUNT` | `152` | `const char *mount_point` | Mounts FAT/LittleFS filesystem on this block device to target VFS path. |
| `EOS_STORAGE_IOCTL_UMOUNT` | `153` | `none` | Flushes cached sectors and unmounts filesystem from VFS. |

---

## 4. Partitioning & Filesystem Mounting (`diskpart.c`)

When a storage driver attaches, the kernel partitioning engine (`main/ecore/diskpart.c`) automatically inspects Master Boot Record (MBR) and GUID Partition Table (GPT) structures:
1. Validates boot sector signature (`0x55AA`).
2. Detects partition boundaries, types, and file formats (FAT12, FAT16, FAT32, exFAT).
3. Automatically exposes sub-partitions as indexed sub-devices (e.g. `/dev/sd0p1`).
4. Mounts volumes to target VFS paths upon receiving `EOS_STORAGE_IOCTL_MOUNT` (e.g. via `/bin/mount /dev/sd0 /sdcard`).

---

## 5. Declarative Attachment Examples

```sh
# Lilka v2.3: MicroSD card on SPI host 1
devctl attach storage sd spi0 --pin cs=16 --cfg max_freq_khz=20000

# Guition JC4880P4: MicroSD card on SDMMC Slot 0 (4-bit @ 40 MHz)
devctl attach storage sd sdio0 --cfg slot=0,width=4,freq=40000000

# QEMU Target: Emulated root flash storage
devctl attach storage flash root --cfg partition="storage"
```
