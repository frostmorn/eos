# Kernel Core Subsystems & Unified Device Model

The EOS kernel core (`main/ecore/`) implements a lightweight, POSIX-compliant operating system abstraction layer running atop Espressif's ESP-IDF. Its foundational principle is the **Unified Device Model** and the **"Everything is a file"** philosophy, bridging physical hardware peripherals, in-memory filesystems, and native applications into a cohesive hierarchical structure.

---

## 1. Device Tree & Hierarchy (`dev.c`, `dev.h`)

EOS organizes all hardware and virtual devices into a hierarchical **Device Tree** modeled as an n-ary tree using `emisc/fancytree.h`. 
*   **Root Anchor (`root`):** The root node (`eos_devtree_root()`) represents the logical top of the device tree, managed by the root bus driver (`edriver/bus/root/root.c`).
*   **Parent-Child Attachment:** Devices attach hierarchically via `eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent)`. Before attachment, EOS verifies that the parent driver has `is_bus == true` (or validates via `attach_req`) and queries the parent bus using ioctl attachment protocols (`EOS_BUS_IOCTL_KID_ATTACH`).
*   **Dynamic Indexing & Naming:** Upon successful attachment, EOS automatically assigns a per-scope stable device ID (`eos_dev_assign_id`) and constructs a canonical device name (`eos_dev_assign_name`, e.g., `fb0`, `gpio`, `sd0`) used by `devfs`.
*   **Recursive Detachment:** Detaching a device (`eos_dev_detach`) recursively detaches all child nodes, shuts down drivers, reporphans orphaned child nodes if permitted, and zeroes device slots for reuse.

---

## 2. Driver Abstraction Layer (`driver.c`, `driver.h`)

Every driver in EOS (`eos_drv_t`) is statically registered at compile-time into the `.eos_drivers` linker section (managed by `eld/driver.lf`) using the `EOS_DRIVER_REG` or direct attribute macros (`EOS_DRV_ATTR`).
*   **Unified VFS Mapping:** Each driver exposes standard POSIX file operations (`open`, `close`, `read`, `write`, `lseek`, `ioctl`, `fstat`, `stat`, `opendir`, `readdir`, etc.).
*   **Dispatch Wrappers:** `ecore/driver.c` provides safe dispatcher wrappers (`eos_drv_read`, `eos_drv_write`, `eos_drv_ioctl`, etc.) that handle null checks, error logging (`EOS_LOGW`), and errno setting (`ENOSYS`).
*   **Cross-Driver Communication (`ioctl`):** Drivers interact with parent buses or sibling subsystems via `eos_dev_ioctl_call` and framework-reserved ioctl commands (e.g., `EOS_IOCTL_GET_DEV`, `EOS_STORAGE_IOCTL_GET_SECTOR_SIZE`).
*   **Scaffolding & Analysis:** New drivers can be generated interactively via `make driver` (see `tools/codegen.py` and [Development Guide](../DEVELOPMENT.md)).

---

## 3. Capability Manager (`capsmgr.c`, `capsmgr.h`)

Hardware resources on ESP32 chips (such as GPIO pins, I2C ports, SPI hosts, UART channels, and LEDC PWM channels) are governed by the **Capability Manager (`capsmgr`)**.
*   **Conflict Prevention:** `eos_cap_claim(eos_cap_type_t type, int32_t no, eos_dev_t *dev)` checks and records resource ownership. Attempting to claim an already-claimed resource returns `false`, preventing driver misconfiguration or pin conflicts.
*   **Reserved Hardware Protection:** At initialization (`eos_capsmgr_init()`), system-reserved pins (strapping pins, flash/PSRAM pins, UART console pins, USB pins) are automatically claimed with a `NULL` owner to block accidental peripheral assignment.
*   **Ownership Transfer:** Claims made by `NULL` (system reserved) can be transferred to specific device drivers when explicitly overridden by board initialization routines.

---

## 4. Thread Context & Resource Tracking (`threadctx.c`, `threadctx.h`)

EOS implements robust per-thread context management (`eos_tctx_t`) stored in FreeRTOS Thread Local Storage (TLS).
*   **Resource Leak Detection & Cleanup:** Every thread tracks open file descriptors (`fds`), opened directories (`dirs`), and allocated memory blocks (`memblocks`).
*   **Automatic Cleanup on Exit:** When a thread (whether spawned via `pthread_create` or FreeRTOS task wrappers) terminates, EOS automatically closes leaked file descriptors, closes leaked directories, frees leaked memory blocks, and logs warnings, preventing resource exhaustion in long-running embedded applications.
*   **Working Directory Isolation:** Each thread maintains its own Current Working Directory (`cwd`), supporting independent path navigation (`chdir`, `getcwd`) across tasks and shell instances.

---

## 5. Path Resolution & FAPI (`fapi.c`, `fapi.h`)

The File API helper module (`fapi`) provides robust path normalization and resolution.
*   **In-Place Normalization:** `eos_fapi_path_normalize` collapses `.` and `..` segments safely in-place without dynamic allocations or external scratch buffers.
*   **CWD-Relative Resolution:** `eos_fapi_path_resolve` automatically translates relative paths into absolute paths based on the calling thread's current working directory.
*   **Core-Safe Scratch Buffers:** Multi-core safe static buffer pools (`eos_fapi_buffer`) prevent race conditions during nested path transformations across CPU cores.

---
[Back to Documentation Index](./Readme.md) | [Development Guide](../DEVELOPMENT.md)
