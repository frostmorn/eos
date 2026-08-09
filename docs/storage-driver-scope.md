# Storage Driver Scope

Storage drivers (e.g., SD Card via SPI) manage persistent data access through the standard device interface.

## Key Responsance

*   **Bus Dependency:** Typically attached to an SPI or SDIO bus node.
*   **Block Access:** Drivers handle block-level read/write operations.
*   **Integration:** Often works in tandem with the **VFS (devfs)** to provide a filesystem interface for mounted media.

## Caveats

1. Since ``off_t`` in esp-idf is defined as 32-bit signed value, it seems impossible to address each byte, cause
it reduces theoretical maximum of addressable space to 2 GB. Cause of that, any storage device is specific in a sense
that each ``read()`` and ``write()`` can't be called with more than 2 GB buffer at once, and ``lseek()`` works offsets 
relative to sector.

Therefore there should be implemented

``EOS_STORAGE_IOCTL_GET_SECTOR_SIZE`` and ``EOS_STORAGE_IOCTL_GET_CAPACITY`` to effectively work with that **specific** file.

---
[Back to EOS Project Index](../Readme.md) | [See Filesystem Drivers](./filesystem-driver-scope.md)
