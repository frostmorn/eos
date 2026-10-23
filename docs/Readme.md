# EOS Documentation Index

Welcome to the EOS (ESP Operating System) documentation center. Below is the flat index of all architectural guides, scope definitions, and subsystem specifications.

---

## Topic Index

*   [Bus Drivers & Attachment Policies](./bus-driver-scope.md) — Topological roles, parent-child permissions, locking mechanisms, and reserved ioctl ranges.
*   [Display Drivers & Framebuffer Architecture](./display-scope.md) — Frame formats, display hot-swapping, and scan-out buffer rendering.
*   [Sound & Audio Codec Drivers](./sound-driver-scope.md) — PCM audio streaming, I2S/SPI/I2C peripheral integration, and device control.
*   [Storage & Block Device Drivers](./storage-driver-scope.md) — SD/SDIO cards, sector-level access, MBR/GPT disk partitioning, and `off_t` handling.
*   [Virtual Filesystem (VFS) & Subsystems](./filesystem-driver-scope.md) — `devfs` hierarchical node mapping and `binfs` statically linked native application execution.
*   [Development & Build Guide](../DEVELOPMENT.md) — Environment prerequisites, board configuration tools (`setboard.py`), and QEMU execution workflows.

---
[Back to Project Root](../Readme.md)
