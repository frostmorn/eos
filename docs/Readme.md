# EOS Project: ESP Operating System

EOS is an advanced lightweight abstraction layer and operating system for ESP32 (specifically targeting ESP32-S3) implementing a **Unified Device Model**. It transforms heterogeneous hardware peripherals into a consistent, hierarchical **Device Tree** accessible via a standard **Virtual Filesystem (VFS)** interface (`/dev`), adhering to the "Everything is a file" philosophy.

---

## Core Architecture & Subsystems

*   [**Bus Drivers**](./bus-driver-scope.md): Implementation guidelines for bus scopes (SPI, I2C, GPIO, root) and parent-child attachment policies.
*   [**Display Drivers**](./display-scope.md): Framebuffer management, display hot-swapping, and graphics output integration.
*   [**Sound Drivers**](./sound-scope.md): Audio codec streaming, PCM data pipelines, and peripheral control.
*   [**Storage Drivers**](./storage-driver-scope.md): Persistent storage, block devices, SD card access, and disk partitioning.
*   [**Virtual Filesystem (VFS)**](./filesystem-driver-scope.md): The underlying `devfs` and `binfs` layers providing device nodes and executable binary blobs.

---
*This document serves as the primary navigation router for the EOS project documentation.*
