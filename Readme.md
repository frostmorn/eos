# EOS Project: ESP Operating System

EOS is an advanced lightweight operating system and abstraction layer for the ESP32 (specifically targeting ESP32-S3). It implements a **Unified Device Model**, transforming heterogeneous hardware peripherals into a hierarchical **Device Tree** accessible via a standard **Virtual Filesystem (VFS)** interface under `/dev`.

---

## Core Design Goals

1. **Root Directory VFS Handling:** Unlike standard ESP-IDF limitations prohibiting direct root directory (`/`) mounting, EOS utilizes `eos_vfs_register()` and linker-based wrappers to intercept and manage virtual filesystem registrations cleanly.
2. **Compile-Time Registration:** Eliminates central maintenance of driver and application lists by utilizing custom linker scripts located in `main/eld` for automatic compile-time registration.
3. **"Everything is a File" Philosophy:** Standardizes hardware control through POSIX I/O (`open`, `close`, `read`, `write`, `lseek`) and unified, scope-specific `ioctl()` commands.
4. **Resource Leak Prevention:** Automatically tracks and cleans up system resources (file descriptors, directories, and thread contexts) per application to guarantee high system stability and survival.

---

## Topic Index

*   [Development Guide](./DEVELOPMENT.md) — Environment setup, target board configuration, build commands, and debugging workflows.
*   [Bus Drivers Scope](./docs/bus-driver-scope.md) — Topological bus roles (SPI, I2C, GPIO, root), permissions, locking, and IOCTL ranges.
*   [Display Drivers Scope](./docs/display-scope.md) — Framebuffer management, scan-out buffers, and display hot-swapping.
*   [Sound Drivers Scope](./docs/sound-driver-scope.md) — I2S/I2C/SPI audio codecs, PCM streaming, and gain/sample-rate configuration.
*   [Storage Drivers Scope](./docs/storage-driver-scope.md) — Persistent storage, SD/SDIO cards, block access, partitioning, and 2GB `off_t` limits.
*   [Virtual Filesystem (VFS)](./docs/filesystem-driver-scope.md) — `devfs` device tree mapping and `binfs` memory-mapped application binaries.
