⚠️ IMPORTANT: Usage Instructions
Before performing any analysis or development task on this project, you MUST load the following skills using the `skill` tool:
- `analyze-c-project`: For initial codebase exploration and mapping.
- `maintain-project-knowledge`: For continuous technical documentation updates.

# EOS Project: Knowledge Base & Architectural Progress
...
## 1. High-Level Concept
EOS is an abstraction layer for ESP32 that implements a **Unified Device Model**. It transforms heterogeneous hardware peripherals into a consistent, hierarchical **Device Tree** accessible via a standard **Virtual Filesystem (VFS)** interface (`/dev`).

---

 2. Core Kernel Mechanisms (`main/ecore/`)

### A. Hierarchical Device Tree (`device.c`, `device.h`)
*   **Structure:** Uses an array of `eos_dev_t` structures representing nodes in a tree. Each node contains pointers to `parent`, `child`, and `next` (sibling).
*   **Identity Generation:** 
    *   **ID:** An incremental integer assigned per scope/parent combination.
    ical: `dev->id = idx`.
    *   **Name:** Derived from the driver's `scope` and the device's `id` (e.g., `spi0`, `display1`).
*   **Attachment Logic (`eos_dev_attach`):** 
    1.  Calls `ioctl(EOS_BUS_IOCTL_KID_ATTACH)` on the parent bus to allow/deny attachment.
    2.  Updates the parent's child linked list.
    3.  Triggers the driver's `.init()` hook.
*   **Detachment Logic (`eos_dev_detach`):** Recursively detaches children, calls the driver's `.shutdown()`, and unlinks from the parent.

### B. Capability Management (`capsmgr.c`, `capsmgr.h`)
*   **Purpose:** A resource reservation system to prevent hardware conflicts (e.g., two drivers claiming the same GPIO).
*   **Mechanism:** Uses an array of `eos_cap_slot_t` to track which `eos_dev_t` owns which hardware index (GPIO pin, SPI host, etc.).
*   **Implementation Detail:** Utilizes C macros (`EOS_CAP_ALLOC_FN`) to auto-generate type-specific allocator functions for GPIO, I2/C, SPI, and UART.
*   **Allocation Flow:** `eos_cap_alloc` $\rightarrow$ Check bounds $\rightarrow$ Check reserved pins $\rightarrow$ Claim slot in global registry.

### C. Virtual Filesystem Implementations (`devfs.c`, `binfs.c`)
EOS implements two distinct VFS layers via the ESP-IDF `esp_vfs_register` API:

#### **1. devfs (Device Filesystem)**
*   **Function:** Maps hardware nodes to pathnames under `/dev/`.
*   **Path Resolution (`devfs_path_to_dev`):** Performs a flat lookup in the global `eos_devices[]` array, matching the provided path string against the device's name.
*   **Operations:** Implements `open`, `close`, `read`, `write`, `ioctl`, and `readdir`.
*   **Directory Listing:** `devfs_readdir` iterates through the `eos_devices` array and yields only "leaf" devices (those that are `in_use` and have a driver, but no children).

#### **2. binfs (Binary Filesystem)**
*   **Function:** A mechanism to execute "Native Apps" stored in memory as binary blobs.
*   **Mechanism:** Uses `eos_native_app_manifest_t` structures. Each app file starts with a unique magic number (`0x0C0A00AC`).
*   **Execution Flow (`eos_system`):** 
    1.  Parse command line into `argc`/`argv`.
    2.  Locate the file in `/bin/` via `binfs`.
    3.  Read the magic number and manifest metadata (name, entry point).
    4.  Directly invoke the `entry_point` function pointer with the parsed arguments.

---

## 3. Driver Architecture (`main/edriver/`)

*   **Standardized Interface (`driver.h`):** Every driver adheres to a `const eos_driver_t` structure containing:
    *   `init`/`shutdown`: Lifecycle hooks.
    *   `read`/`write`: Data stream I/O.
    *   `ioctl`: Control interface for device-specific commands (e.g., `EOS_DISPLAY_IOCTL_GET_WIDTH`).
*   **Bus vs. Peripheral Pattern:** 
    *   **Bus Drivers (SPI, I2C):** Act as anchors in the tree. They handle "attachment" ioctls to register child devices onto their physical bus.
s
    *   **Peripheral Drivers (ST7789, SD Card):** Attach themselves to a Bus node. They rely on the parent's configuration (e.g., `host` ID) and manage their own pins/segments.

---

## 4. Board Abstraction (`main/eboard/`)

*   **Declarative Topology:** Hardware setup is performed in `eos_board_init()` by:
    1.  Allocating a device node (`eos_dev_alloc`).
    2.  Assigning a driver via `eos_driver_find`.
    3.  Defining pin maps using `eos_pin_t` arrays (e.g., `{"sclk", 18}, {"mosi", 17}`).
    4.  Providing configuration parameters via `eos_cfg_t` (e.g., `clock_speed_hz`).
    5.  Attaching the node to a parent or the root (`eos_dev_attach`).

---

## 5. Summary of Key Data Structures

| Structure | Purpose | Key Members |
| :--- | :--- | :--- |
| `eos_dev_t` | Hardware Node | `driver`, `pins`, `cfg`, `state`, `parent`, `child`, `next`, `name` |
| `eos_driver_t` | Driver Interface | `init`, `read`, `write`, `ioctl`, `shutdown`, `scope`, `name` |
| `eos_cap_slot_t`| Resource Tracker| `cap`, `cap_no`, `in_use`, `owner_dev` |
| `eos_native_app_manifest_t` | App Metadata | `magic`, `filename`, `name`, `entry_point` |

