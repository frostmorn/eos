# EOS Project: Hardware Abstraction Layer

EOS is an abstraction layer for ESP32 that implements a **Unified Device Model**. It transforms heterogeneous hardware peripherals into a consistent, hierarchical **Device Tree** accessible via a standard **Virtual Files/system (VFS)** interface (`/dev`).

## Core Architecture

*   [**Bus Drivers**](./docs/bus-driver-scope.md): How buses and anchors are implemented.
*   [**Display Drivers**](./docs/display-scope.md): Graphics and framebuffer management.
*   [**Sound Drivers**](./docs/sound-scope.md): Audio streaming and codec integration.
*   [**Storage Drivers**](./docs/storage-driver-scope.md): SD Card and persistent storage access.
*   [**Virtual Filesystem (VFS)**](./docs/vfs-scope.md): The `devfs` and `binfs` implementation layers.

---
*This document serves as the primary router for the EOS project documentation.*
