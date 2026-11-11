# EOS Documentation Center

Welcome to the **EOS (ESP Operating System)** documentation center. EOS is a lightweight operating system and abstraction layer for Espressif microcontrollers (specifically ESP32-S3 and ESP32-P4) implementing a **Unified Device Model** ("Everything is a file").

This documentation center is structured into dedicated topic directories for everyday development, driver authoring, hardware porting, and architectural reference.

---

## Documentation Structure

```text
docs/
├── Readme.md                          # Master documentation index & quick reference
├── binaries/                          # Executable binaries & execution environment
│   ├── catalog.md                     # Everyday command catalog (all 55 binaries with options & syntax)
│   ├── execution-architecture.md      # BinFS (/bin), statically linked .eos_bins, ush shell, execve
│   ├── manifest-flags-and-stacks.md   # eos_bin_t flags, PSRAM stack routing, graceful SRAM fallback
│   └── elf-applications.md            # Dynamic ELF loading, standalone apps, compilation & runtime symbols
├── drivers/                           # Driver contracts, IOCTL tables, scopes & lifecycles
│   ├── overview.md                    # Unified driver contract (eos_drv_t), lifecycle, capsmgr, VFS
│   ├── bus.md                         # Bus scope (root, spi, i2c, sdio, usb, gpio) & bus IOCTLs
│   ├── display.md                     # Display scope (st7789, st7701s, qemu_rgb) & framebuffer IOCTLs
│   ├── input.md                       # Input scope (gpio_keyboard, gt911), event models, synthetic injection
│   ├── sound.md                       # Sound scope (max98357), audio streaming, sample rates, power mult
│   ├── storage.md                     # Storage scope (sd, flash), block IOCTLs, partitions, 2GB limits
│   ├── network.md                     # Network scope (wifi, sta, ap, bridge), radio lifecycle, IOCTLs
│   └── adc-and-power.md               # ADC bus & battery power drivers, voltage curve, SoC engine
├── architecture/                      # Overall system architecture
│   ├── kernel-core.md                 # Unified Device Model, Device Tree, capsmgr, proc, fapi, asset
│   ├── vfs-and-filesystems.md         # VFS hierarchy: tmpfs (/), devfs (/dev), binfs (/bin), pipefs, procfs
│   ├── wrappers-and-syscalls.md       # Linker wrappers (-Wl,--wrap), memory leak tracking, reentrant syscalls
│   └── memory-and-stacks.md           # Internal SRAM vs External PSRAM, DMA constraints, kvec optimization
├── hardware/                          # Hardware targets & board specifications
│   ├── board-porting.md               # Declarative hardware architecture (init.sh, sdkconfig), setboard.py
│   ├── targets.md                     # Hardware target reference (lilka_v23, jc4880p443c_i_w_y, esp32s3_qemu)
│   └── jc4880p4-coprocessor.md        # Guition JC4880P4 (ESP32-P4 + C6) ESP-Hosted SDIO co-processor guide
├── roadmap/                           # System roadmap, task breakdown & architectural decisions
│   ├── tasks.md                       # Comprehensive task specification (F01-F08 foundations, T01-T22 features)
│   ├── implementation-order.md        # Execution phases (0-6), dependency graph, critical paths, milestones
│   └── decisions-and-audit-rules.md   # Architectural decisions (D1-D15), test strategy & declarative audit rules
└── reports/                           # Historical & cleaned-up technical reports & audits
    ├── ebin-audit-and-roadmap.md      # Audit of ebin utilities, implemented coreutils, evolution roadmap
    └── psram-heap-migration-report.md # PSRAM heap allocation audit, DMA constraints, and completed status
```

---

## 1. Binaries & Command-Line Tools (`docs/binaries/`)

* [**Executable Binaries Catalog**](./binaries/catalog.md) — Comprehensive reference for all 55 native binaries:
  * **Core Utilities:** `cat`, `cd`, `cp`, `echo`, `ls`, `mkdir`, `mv`, `pwd`, `rm`, `rmdir`, `sleep`, `touch`, `tree`, `write`.
  * **Checksums & Encoding:** `md5sum`, `sha1sum`, `sha224sum`, `sha256sum`, `sha384sum`, `sha512sum`, `cksum`, `base64`.
  * **System & Shell:** `ush`, `launcher`, `mount`, `umount`.
  * **Hardware & Bus Control:** `battery`, `devctl`, `lsi2c`, `soldercheck`.
  * **Network Management:** `netctl`, `wlanctl`.
  * **GUI Applications:** `lvgl_calc`, `lvgl_keypad`, `lvgl_net`, `lvgl_snake`, `lvgl_sysmon`, `lvgl_term`.
  * **Diagnostics & Tests:** `argvexpose`, `fdsetcheck`, `membench`, `test_coreutils`, `test_hash`, `test_sound`, `test_input`, `test_lvgl`, `test_sdl`, etc.
* [**Hash, Checksum & Base64 Tools**](./binaries/hash-and-checksum-tools.md) — `md5sum`/`sha*sum`/`cksum`/`base64`: shared library, check-list format, differences from GNU coreutils, memory behaviour, and how the tools are tested.
* [**BinFS, Native Binaries & Execution**](./binaries/execution-architecture.md) — Linker section `.eos_bins`, script assets (`init.sh`), execution pipeline (`exec.c`), MicroShell (`ush.c`), pipes, and redirections.
* [**Dynamic ELF Applications & Runtime Loader**](./binaries/elf-applications.md) — Standalone dynamic apps, runtime symbol resolution, PSRAM execution, app compilation, and toolchain discovery.
* [**Ebin Manifest Flags & Stack Architecture**](./binaries/manifest-flags-and-stacks.md) — `eos_bin_t` manifest flags (`EOS_BIN_FLAG_GUI`, `EOS_BIN_FLAG_STACK_PSRAM`, `EOS_BIN_FLAG_STACK_SRAM`), stack dimensioning, PSRAM routing, and graceful SRAM fallback.

---

## 2. Driver Contracts & IOCTL Interfaces (`docs/drivers/`)

* [**Driver Contract & Model Overview**](./drivers/overview.md) — The `eos_drv_t` contract, compile-time `.eos_drivers` registration, device lifecycle (`init`/`shutdown`), state encapsulation, capability manager (`capsmgr`), and VFS operations.
* [**Bus Drivers & Attachment Policies**](./drivers/bus.md) — Bus scope (`root`, `spi`, `i2c`, `sdio`, `usb`, `gpio`), `EOS_BUS_IOCTL_*` attachment and locking contracts, GPIO and I2C IOCTLs.
* [**Display Drivers & Framebuffer Architecture**](./drivers/display.md) — `st7789`, `st7701s`, `qemu_rgb`, RGB565 scanout format, display geometry IOCTLs, and DMA constraints.
* [**Input Drivers & Event Architecture**](./drivers/input.md) — `gpio_keyboard`, `gt911`, standardized `eos_input_event_t`, non-blocking `EOS_KEYBOARD_IOCTL_POLL`, and synthetic event injection via `write()`.
* [**Sound & Audio Codec Drivers**](./drivers/sound.md) — `max98357` I2S driver, `EOS_SOUND_IOCTL_*` contract, fast bit-shift scaling, DMA quad-buffering, acoustic matching (22,050 Hz default), and hearing protection multipliers.
* [**Storage & Block Device Drivers**](./drivers/storage.md) — SD card (`sd`) and raw flash (`flash`), sector read/write operations, capacity IOCTLs, 32-bit `off_t` limitations, and partition table parsing (`diskpart.c`).
* [**Network Drivers & TCP/IP Architecture**](./drivers/network.md) — Wi-Fi physical radio (`wifi`), STA mode (`sta`), AP mode (`ap`), network bridge (`bridge`), parent-child IOCTL coordination, and promiscuous 802.11 sniffer queue.
* [**ADC Bus Driver & Battery Power Driver**](./drivers/adc-and-power.md) — `adc` bus and `battery` driver, raw/calibrated voltage IOCTLs, Lilka resistor divider, 12-point empirical LiPo curve, and state-of-charge engine.

---

## 3. Overall System Architecture (`docs/architecture/`)

* [**Kernel Core Subsystems & Unified Device Model**](./architecture/kernel-core.md) — Hierarchical Device Tree (`dev.c`), driver abstraction (`driver.c`), capability manager (`capsmgr.c`), process table and context management (`proc.c`), path resolution (`fapi.c`), and static assets (`asset.c`).
* [**Virtual Filesystem (VFS) & Subsystems**](./architecture/vfs-and-filesystems.md) — Root `tmpfs` (`/`), dynamic `devfs` (`/dev`), executable `binfs` (`/bin`), diagnostic `procfs` (`/proc`), and inter-process `pipefs` (`/proc/pipes`).
* [**POSIX, FreeRTOS & Syscall Wrappers**](./architecture/wrappers-and-syscalls.md) — Linker-wrapped syscalls (`-Wl,--wrap`), memory allocation leak tracking (`heap_caps.c`), reentrant filesystem syscalls, thread creation (`pthread.c`), and SDL 1.2/2.0 compatibility layers.
* [**Embedded Memory Architecture & Stack Allocation**](./architecture/memory-and-stacks.md) — Physical memory domains (Internal SRAM vs External PSRAM), allocation capabilities, DMA constraints, stack dimensioning, and Pratt parser PSRAM migration (+56.3 KB SRAM saved).

---

## 4. Hardware Targets & Specifications (`docs/hardware/`)

* [**Board Porting & Declarative Architecture**](./hardware/board-porting.md) — Declarative hardware model (`init.sh`, `sdkconfig`), board synchronization tool (`tools/setboard.py`), and `devctl attach` syntax.
* [**Hardware Target Profiles & Pinouts**](./hardware/targets.md) — Detailed hardware specifications, block diagrams, and pin mappings for `lilka_v23` (ESP32-S3), `jc4880p443c_i_w_y` (ESP32-P4), and `esp32s3_qemu`.
* [**Guition JC4880P4 Co-Processor Guide**](./hardware/jc4880p4-coprocessor.md) — Split-architecture ESP32-P4 + ESP32-C6 design, ESP-Hosted SDIO protocol, JP1 Expand-IO header pinout, and co-processor flashing procedure.

---

## 5. Technical Reports & Audits (`docs/reports/`)

* [**Executable Binaries Audit & Roadmap**](./reports/ebin-audit-and-roadmap.md) — Comprehensive inventory of all commands, review of completed core POSIX utilities (`rm`, `cp`, `mv`, `sleep`, `test_coreutils`), and future roadmap.
* [**PSRAM Heap Migration Report**](./reports/psram-heap-migration-report.md) — Memory audit across kernel core and drivers, DMA hardware constraints, and completed PSRAM migrations (+56.3 KB SRAM from Pratt parser, 48–64 KB from sniffer queue, 16 KB per GUI stack).

---

## 6. System Roadmap, Decisions & Governance (`docs/roadmap/`)

* [**Comprehensive Task Specification**](./roadmap/tasks.md) — Detailed breakdown of foundation tasks (F01–F08) and functional epics (T01–T22: Terminal, Observability, Filesystems, Security, Drivers, Network Libraries, Apps & Loader).
* [**Implementation Order & Phasing**](./roadmap/implementation-order.md) — Strategic execution sequence across Phases 0–6, dependency tree, critical paths, and milestone criteria.
* [**Architectural Decisions & Audit Rules**](./roadmap/decisions-and-audit-rules.md) — Pre-implementation decisions (D1–D15), test harness classification (`qemu` vs `hardware-only`), and declarative `layer_audit` enforcement rules.
* [**Architecture & Status Matrix**](./status.md) — Auto-generated verification matrix tracking peripheral driver states, core kernel subsystems, test suite coverage, and board support.

---

## Everyday Developer Cheat Sheet

```bash
# 1. Target Board Selection:
make setboard                                    # Interactive board selector
python3 tools/setboard.py lilka_v23              # Select Lilka v2.3 (ESP32-S3)
python3 tools/setboard.py jc4880p443c_i_w_y      # Select Guition JC4880P4 (ESP32-P4)
python3 tools/setboard.py esp32s3_qemu           # Select Espressif QEMU Emulator

# 2. Build & Flash:
source /opt/esp-idf/export.sh                    # Load ESP-IDF environment
make build                                       # Compile firmware image
make flash                                       # Flash connected device over serial
make monitor                                     # Open serial console monitor
make qemu                                        # Run in headless QEMU emulator
make qemu_graphics                               # Run in QEMU with SDL2 graphical display

# 3. Interactive Code Generation:
make driver                                      # Scaffold new driver under main/edriver/
make bin                                         # Scaffold new binary under main/ebin/
make app                                         # Scaffold new standalone dynamic ELF application under apps/

# 4. Verification & Testing:
python3 tools/layer_audit.py                     # Verify 0 layer violations
python3 -u tools/hardware_test.py -p /dev/ttyACM0 --suite all  # Run hardware test suite
```

---
[Back to Project Root](../Readme.md) | [See Development Guide](../AGENTS.md)
