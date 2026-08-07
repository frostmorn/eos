# Storage Driver Scope

Storage drivers (e.g., SD Card via SPI) manage persistent data access through the standard device interface.

## Key Responsance

*   **Bus Dependency:** Typically attached to an SPI or SDIO bus node.
*   **Block Access:** Drivers handle block-level read/write operations.
*   **Integration:** Often works in tandem with the **VFS (devfs)** to provide a filesystem interface for mounted media.

---
[Back to EOS Project Index](../Readme.md) | [See Filesystem Drivers](./filesystem-driver-scope.md)
