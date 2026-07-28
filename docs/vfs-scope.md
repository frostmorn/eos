# Virtual Filesystem (VFS) Scope

The **TMP Virtual Filesystem** provides a standardized interface for interacting with hardware as if it were a file system. It implements two primary layers: `devfs` and `binfs`.

## 1. devfs (Device Filesystem)

Maps hardware nodes to pathnames under `/dev/`.
*   **Path Resolution:** Uses a flat lookup in the global `eos_devices[]` array.
*   **Operations:** Supports standard `open`, `close`, `read`, `write`, and `ioctl`.
*   **Directory Listing:** `devfs_readdir` iterates through devices, yielding "leaf" devices (those with active drivers but no children).

## 2. binfs (Binary Filesystem)

A mechanism to execute "Native Apps" stored as binary blobs in memory.
*   **Manifests:** Each app starts with a unique magic number (`0:0A00AC`).
*   **Execution Flow:** The `eos_system` locates the file in `/bin/`, parses metadata (name, entry point), and invokes the function pointer.

---
[Back to EOS Device Model Index](../Readme.md) | [See Bus Drivers](../bus-driver-scope.md)
