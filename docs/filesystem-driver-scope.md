# Virtual Filesystem (VFS) Scope & Subsystems

The EOS Virtual Filesystem provides a unified, standardized POSIX-compliant interface for interacting with system resources, hardware peripherals, and executable applications, adhering firmly to the philosophy that "Everything is a file". It integrates two primary filesystem layers: `devfs` and `binfs`, mounted atop the root `tmpfs`.

---

## 1. devfs (Device Filesystem)

`devfs` dynamically maps hierarchical device tree nodes to pathnames under `/dev/`.
*   **Path Resolution & Device Tree:** Resolves device paths hierarchically by traversing parent-child relationships in the device tree managed by `dev.c`.
*   **POSIX Operations:** Exposes standard file operations (`open`, `close`, `read`, `write`, `lseek`, and `ioctl`), forwarding them through the driver abstraction layer (`driver.c`).
*   **Directory Enumeration:** `devfs_readdir` iterates through device tree nodes, presenting active device nodes as filesystem entries.

---

## 2. binfs (Binary Filesystem)

`binfs` provides an in-memory execution environment for statically linked system commands and native applications residing under `/bin/`.
*   **Statically Linked Binaries:** Applications are registered at compile-time into the `.eos_bins` linker section using `EOS_BIN_INITIALIZER` macros.
*   **Execution Flow:** The user shell or execution engine locates binary apps within `binfs`, resolves metadata (name, entry point function pointer), and spawns them within isolated thread contexts (`threadctx.c`).

---
[Back to EOS Project Index](../Readme.md) | [See Bus Drivers](./bus-driver-scope.md)
