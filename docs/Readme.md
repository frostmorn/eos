# EOS Documentation Center

Welcome to the **EOS (ESP Operating System)** documentation center. Below is the comprehensive index of all architectural guides, subsystem specifications, and driver scopes for the ESP32-S3 lightweight operating system and Unified Device Model.

---

## Architecture & Subsystem Guides

*   [Kernel Core Subsystems & Unified Device Model](./kernel-core-scope.md) — Device tree hierarchy (`dev.c`), driver abstraction (`driver.c`), capability manager (`capsmgr.c`), thread context tracking (`threadctx.c`), path resolution (`fapi.c`), and asset management (`asset.c`).
*   [Bus Drivers & Attachment Policies](./bus-driver-scope.md) — Topological bus roles (`root`, `spi`, `i2c`, `sdio`, `usb`, `gpio`, `onewire`), parent-child attachment permissions, synchronization locking protocols, and reserved ioctl ranges.
*   [Display Drivers & Framebuffer Architecture](./display-scope.md) — Frame formats (`st7789`, `st7701s`), scan-out buffer rendering, display window ioctl controls, and VFS integration.
*   [Input Drivers & Event Architecture](./input-driver-scope.md) — Standardized input event data models (`eos_input_event_t`), touchscreen (`gt911`), keyboard/keypad active-low polling, and non-blocking IOCTLs.
*   [Sound & Audio Codec Drivers](./sound-driver-scope.md) — PCM audio streaming (`max98357`), I2S/SPI/I2C peripheral integration, and hardware configuration ioctls.
*   [Storage & Block Device Drivers](./storage-driver-scope.md) — SD/SDIO block-level access, flash storage, disk partitioning (`diskpart.c`), `off_t` limitations, and mandatory capability ioctls.
*   [Network Drivers & TCP/IP Architecture](./network-driver-scope.md) — Wi-Fi radio driver (`wifi.c`), STA/AP child drivers (`sta.c`, `ap.c`), bridging (`bridge.c`), and network management binaries (`wlanctl`, `netctl`).
*   [Virtual Filesystem (VFS) & Subsystems](./filesystem-driver-scope.md) — Root `tmpfs`, hierarchical `devfs` node mapping, executable `binfs`, process filesystem `procfs`, and pipe ring-buffers `pipefs`.
*   [BinFS, Native Binaries & Execution](./binfs-and-binaries-scope.md) — Statically linked ELF linker sections (`.eos_bins`), process execution (`exec.c`), microshell (`ush.c`), system call interception, and code generation (`make bin`).
*   [POSIX, FreeRTOS & ESP-IDF Wrappers](./wrappers-architecture.md) — Linker-wrapped syscalls (`-Wl,--wrap`), heap allocation leak tracking (`heap_caps.c`), path remapping, and SDL 1.2/2.0 compatibility layers.
*   [Board Porting & Hardware Targets](./board-porting-scope.md) — Target board configurations (`lilka_v23`, `esp32s3_qemu`, `jc4880p443c_i_w_y`), board initialization (`eos_board_init`), and setup scripts (`tools/setboard.py`, `tools/codegen.py`).
*   [Development & Build Guide](../DEVELOPMENT.md) — Environment prerequisites, ESP-IDF export, compilation commands (`make build`, `make flash`, `make qemu`), analysis tools, and workspace memory & skill guidelines.

---
[Back to Project Root](../Readme.md)
