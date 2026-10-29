# Storage Driver Scope & Block Architecture

Storage drivers (such as SD Cards via SPI/SDIO or raw flash storage) manage persistent data access through the standard EOS device VFS interface.

---

## Key Responsibilities & Integration

*   **Bus Dependency:** Storage peripherals attach as child nodes to SPI (`storage/sd` over `spi0`), SDIO (`storage/sd` over `sdio0`), or root (`storage/flash` over `root`).
*   **Block-Level Access:** Drivers handle sector-level and block-level read/write operations.
*   **Filesystem Integration:** Works in tandem with partitioning layers (`diskpart.c`) and VFS mounting to provide filesystem access (e.g., FATFS) over persistent storage media.

---

## Technical Considerations & Caveats

1.  **Addressing Limits & `off_t`:** Because `off_t` in ESP-IDF is a 32-bit signed integer, the maximum directly addressable byte offset is restricted to 2 GB. Consequently, storage `read()` and `write()` operations are designed to handle block buffers appropriately, and `lseek()` offsets operate relative to sectors/bytes.
2.  **Mandatory IOCTL Commands:** To reliably inspect media capacity and block structure exceeding standard file size limitations, storage drivers must implement:
    *   `EOS_STORAGE_IOCTL_GET_SECTOR_SIZE`
    *   `EOS_STORAGE_IOCTL_GET_CAPACITY`

---
[Back to EOS Project Index](../Readme.md) | [See Filesystem Drivers](./filesystem-driver-scope.md) | [See Bus Drivers](./bus-driver-scope.md)
