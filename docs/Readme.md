# EOS Project: ESP Operating System

EOS is an abstraction layer for ESP32 that implements a **Unified Device Model**. It transforms heterogeneous hardware peripherals into a consistent, hierarchical **Device Tree** accessible via a standard **Virtual Files/system (VFS)** interface (`/dev`).

## Core Architecture

*   [**Bus Drivers**](./bus-driver-scope.md): How buses and anchors are implemented.
*   [**Display Drivers**](./display-scope.md): Graphics and framebuffer management.
*   [**Sound Drivers**](./sound-scope.md): Audio streaming and codec integration.
*   [**Storage Drivers**](./storage-driver-scope.md): SD Card and persistent storage access.
*   [**Virtual Filesystem (VFS)**](./vfs-scope.md): The `devfs` and `binfs` implementation layers.

---
*This document serves as the primary router for the EOS project documentation.*
