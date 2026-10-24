# EOS Documentation Index

Welcome to the EOS (ESP Operating System) documentation center. Below is the comprehensive index of all architectural guides, scope definitions, and subsystem specifications.

---

## Topic Index

*   [Kernel Core Subsystems & Unified Device Model](./kernel-core-scope.md) — Device tree hierarchy, driver abstraction layer, capability manager, thread contexts, and path resolution.
*   [Bus Drivers & Attachment Policies](./bus-driver-scope.md) — Topological roles, parent-child permissions, locking mechanisms, and reserved ioctl ranges.
*   [Display Drivers & Framebuffer Architecture](./display-scope.md) — Frame formats, display hot-swapping, and scan-out buffer rendering.
*   [Input Drivers & Event Architecture](./input-driver-scope.md) — Input event data models, keyboard/keypad drivers, and polling IOCTLs.
*   [Sound & Audio Codec Drivers](./sound-driver-scope.md) — PCM audio streaming, I2S/SPI/I2C peripheral integration, and device control.
*   [Storage & Block Device Drivers](./storage-driver-scope.md) — SD/SDIO cards, sector-level access, MBR/GPT disk partitioning, and `off_t` handling.
*   [Virtual Filesystem (VFS) & Subsystems](./filesystem-driver-scope.md) — `devfs` hierarchical node mapping and `binfs` statically linked native application execution.
*   [BinFS, Native Binaries & Execution](./binfs-and-binaries-scope.md) — Statically linked ELF linker sections (`.eos_bins`), process execution (`exec.c`), and microshell (`ush`).
*   [POSIX, FreeRTOS & ESP-IDF Wrappers](./wrappers-architecture.md) — System call interception, memory leak tracking (`heap_caps.c`), path remapping, and SDL 1.2/2.0 compatibility layers.
*   [Board Porting & Hardware Targets](./board-porting-scope.md) — Target board configurations (`eboard/`), hardware initialization routines, and pin allocation rules.
*   [Development & Build Guide](../DEVELOPMENT.md) — Environment prerequisites, board configuration tools (`setboard.py`), and QEMU execution workflows.

---
[Back to Project Root](../Readme.md)
