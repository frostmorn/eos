# EOS Project: ESP Operating System

Note, this piece of software is in early development stage, therefore, those docs can't be
much meaningful.

Though, there's a minimal list of things I would like to achieve during its development.

1. ESP-IDF do not allow to open Root directory(/), but EOS can. For this purpose currently 
am currently using ``eos_vfs_register()`` intercepting any VFS registration, which at 
some point may be an actual wrapper arround ``esp_vfs_register()`` utilizing magic of 
linker based wrapers, whose now are heavily used to implement other additional features.

2. EOS proposes a self-constructable list of drivers and apps in compile time completely 
removing needness to maintain somewhere central list of all those things, as well as 
an ability to add into it additional data. This is done via linker scripts located in
``main/eld``.

3. Each driver should represent a quant of HAL and unify devices to a ``Everything is a 
FILE`` philosophy. Things whose can't be represented in a form of standard I/O should be 
done via ``ioctl()`` calls specific but unified per each device scope.

4. Each app have a special system whose not allow to leak resources(like file 
descriptors, dirs and probably even memory blocks) increasing system survival.


// Dumb shit to fix list:

1. Since memory allocated before app_main() with malloc can't be freed, we would use pthread as a main thread creating utility.


//////////////////////////////////////////////////////////////
AI generated stuff below ->>
//////////////////////////////////////////////////////////////

EOS is an abstraction layer for ESP32 that implements a **Unified Device Model**. It transforms heterogeneous hardware peripherals into a consistent, hierarchical **Device Tree** accessible via a standard **Virtual Files/system (VFS)** interface (`/dev`).

## Core Architecture

*   [**Bus Drivers**](./docs/bus-driver-scope.md): How buses and anchors are implemented.
*   [**Display Drivers**](./docs/display-scope.md): Graphics and framebuffer management.
*   [**Sound Drivers**](./docs/sound-scope.md): Audio streaming and codec integration.
*   [**Storage Drivers**](./docs/storage-driver-scope.md): SD Card and persistent storage access.
*   [**Virtual Filesystem (VFS)**](./docs/vfs-scope.md): The `devfs` and `binfs` implementation layers.

---
*This document serves as the primary router for the EOS project documentation.*
