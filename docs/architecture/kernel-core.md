# Kernel Core Subsystems & Unified Device Model

The EOS kernel core (`main/ecore/`) implements a lightweight, POSIX-compliant operating system abstraction layer running atop Espressif's ESP-IDF. Its foundational principle is the **Unified Device Model** and the **"Everything is a file"** philosophy, bridging physical hardware peripherals, in-memory filesystems, and native applications into a cohesive hierarchical structure.

---

## 1. Device Tree & Hierarchy (`dev.c`, `dev.h`)

EOS organizes all hardware and virtual devices into a hierarchical **Device Tree** modeled as an n-ary tree using `emisc/fancytree.h`:

* **Root Anchor (`root`):** The root node (`eos_devtree_root()`) represents the logical top of the device tree, managed by the root bus driver (`edriver/bus/root/root.c`).
* **Parent-Child Attachment:** Devices attach hierarchically via `eos_dev_attach(eos_dev_t *dev, eos_dev_t *parent)`. Before attachment, EOS verifies that the parent driver has `is_bus == true` and queries the parent bus using the ioctl attachment protocol (`EOS_BUS_IOCTL_KID_ATTACH`).
* **Dynamic Indexing & Naming:** Upon successful attachment, EOS automatically assigns a per-scope stable device ID (`eos_dev_assign_id`) and constructs a canonical device name (`eos_dev_assign_name`, e.g., `fb0`, `gpio`, `sd0`) mapped into `devfs`.
* **Zero Static Device Allocation:** Global device registry `s_devices` is dynamically tracked in PSRAM using `kvec_psram_t(eos_dev_t *)`, completely eliminating compile-time static array limits (`EOS_MAX_DEVICES`).
* **Dynamic Attachment & Detachment via `devctl`:** Devices can be dynamically attached (`devctl attach`) or detached (`devctl detach`) at runtime. Detaching a device recursively detaches all child nodes, shuts down drivers, releases claimed hardware capabilities, and zeroes device slots for reuse.

---

## 2. Driver Abstraction Layer & Device Tree Operations (`dev.c`, `dev.h`, `driver.c`, `driver.h`)

Every driver in EOS (`eos_drv_t`) is statically registered at compile-time into the `.eos_drivers` linker section (managed by `eld/driver.lf`) using `EOS_DRV_ATTR`.

* **Zero Direct Bus/Device Public APIs:** There are no standalone public C APIs for interacting with buses or devices (such as `scan_i2c_bus()` or `sdio.h`). Userspace applications and utilities interact with hardware exclusively via standard POSIX VFS syscalls (`open`, `read`, `write`, `close`, `ioctl`, `opendir`, `closedir`).
* **Inter-Driver Communication strictly via `eos_dev_*`:** Inter-device interactions (e.g. child device communicating with its parent bus controller, or peer drivers cooperating) must never open `/dev/...` files through VFS. Instead, drivers interact strictly via `eos_dev_*` operations (`dev.h`) on device tree pointers (`dev->parent`):
  - `eos_dev_open()`, `eos_dev_close()`, `eos_dev_read()`, `eos_dev_write()`
  - `eos_dev_lseek()`, `eos_dev_pread()`, `eos_dev_pwrite()`
  - `eos_dev_ioctl()`, `eos_dev_ioctl_call()`
  - `eos_dev_opendir()`, `eos_dev_readdir()`, `eos_dev_closedir()`
* **Unified VFS Mapping:** Each driver exposes standard POSIX file operations mapped into `devfs`. Dispatcher wrappers in `driver.c` handle null checks, error logging (`EOS_LOGW`), and errno propagation (`ENOSYS`).
* **Device Enumeration:** Enumeration of devices on a bus or under `/dev` is performed strictly via directory traversal (`opendir`, `readdir`). Hardware bus probing is triggered via `ioctl` (e.g. `EOS_I2C_IOCTL_PROBE`), populating the bus hierarchy for directory listing.

---

## 3. Capability Manager (`capsmgr.c`, `capsmgr.h`)

Hardware resources on ESP32 microcontrollers (such as GPIO pins, I2C ports, SPI hosts, UART channels, and LEDC PWM channels) are governed by the **Capability Manager (`capsmgr`)**:

* **Conflict Prevention:** `eos_cap_claim(eos_cap_type_t type, int32_t no, eos_dev_t *dev)` checks and records resource ownership. Attempting to claim an already-claimed resource returns `false`, preventing driver misconfiguration or pin conflicts.
* **Reserved Hardware Protection:** At initialization (`eos_capsmgr_init()`), system-reserved pins (strapping pins, flash/PSRAM pins, UART console pins, USB pins) are automatically claimed with a `NULL` owner to block accidental peripheral assignment.
* **Ownership Transfer:** Claims made by `NULL` (system reserved) can be transferred to specific device drivers when explicitly overridden during device initialization or attachment.

---

## 4. Process Table & Context Management (`proc.c`, `proc.h`)

EOS implements a unified, multi-process architecture with a centralized process table (`eos_proc_t`) allocated in PSRAM:

* **Zero FreeRTOS Task Local Storage:** EOS completely eliminates FreeRTOS task local storage (`pvTaskGetThreadLocalStoragePointer`/`vTaskSetThreadLocalStoragePointer`). Mapping between FreeRTOS task handles and process contexts (`eos_proc_t`) is maintained in an internal spinlock-guarded task table.
* **Process Lifecycle & Identification:** Each process instance is assigned a unique `pid`, `ppid`, `pgid`, and `sid`. State is tracked as `EOS_PROC_STATE_RUNNING` (`'R'`), `EOS_PROC_STATE_SLEEPING` (`'S'`), `EOS_PROC_STATE_STOPPED` (`'T'`), or `EOS_PROC_STATE_ZOMBIE` (`'Z'`).
* **POSIX Process Hierarchy & Group Management:** Supports standard POSIX process operations including `getpid()`, `getppid()`, `getpgid()`, `setpgid()`, `setsid()`, and `waitpid()`. When parent processes exit without reaping, child processes are automatically reparented to `init` (PID 1).
* **Multi-Thread Membership:** Each process manages a main thread (`main_thread`) and a dynamic vector of child threads (`threads`). All threads spawned by a process (via `pthread_create` or FreeRTOS task primitives) inherit the parent process's context (`eos_proc_t`) and share its resource registry.
* **Working Directory & Environment Isolation:** Each process maintains its own Current Working Directory (`cwd`), controlling terminal (`tty`), and isolated environment variables (`env`), supporting independent path navigation (`chdir`, `getcwd`) and process-specific variables.
* **Dynamic File Descriptor Maps (`fd_map`):** File descriptor remapping is managed via dynamic PSRAM vectors (`kvec_psram_t(int) fd_map`) accessed through `eos_proc_fd_map_get` and `eos_proc_fd_map_set`, eliminating fixed compile-time arrays.
* **Detached Background Execution & GUI Kiosk Model:**
  - Background processes can be spawned asynchronously (`&`) without capturing shell stdin/stdout, allowing uninterrupted interactive operation.
  - The kiosk desktop environment (`launcher`) runs as a persistent detached background supervisor. Interactive applications (such as `lvgl_net`) are launched asynchronously via IPC (`/tmp/launcher.cmd`), keeping the interactive POSIX shell responsive and serial logging streamed to stdout.
* **Unified Resource Leak Tracking & Zombie Reaping:**
  - Open file descriptors (`fds`) and descriptor mapping (`fd_map`) for I/O redirections and pipes.
  - Open directory handles (`dirs`).
  - Allocated dynamic memory blocks (`memblocks`) protected by `mem_mutex` (internal SRAM).
  - Cross-process deallocation safety: `eos_proc_mem_remove` searches across active processes in the process table to safely track and unregister cross-process deallocations (e.g. `execve` parameter transfer or system task memory reclamation).
  - Internal kernel allocation guards: `eos_proc_heap_tracking_disable()` and `eos_proc_heap_tracking_enable()` protect kernel internal parameter staging and VFS structures from polluting user process memory registries.
  - When all threads of a process terminate, the process transitions to zombie state until reaped via `waitpid()`, at which point all unreleased file descriptors, directories, and memory blocks are reclaimed automatically.

---

## 5. Path Resolution & FAPI (`fapi.c`, `fapi.h`)

The File API helper module (`fapi`) provides path normalization and resolution:

* **In-Place Normalization:** `eos_fapi_path_normalize` collapses `.` and `..` segments safely in-place without dynamic allocations or external scratch buffers.
* **CWD-Relative Resolution:** `eos_fapi_path_resolve` automatically translates relative paths into absolute paths based on the calling thread's current working directory.
* **Core-Safe Scratch Buffers:** Multi-core safe static buffer pools (`eos_fapi_buffer`) prevent race conditions during nested path transformations across CPU cores.

---

## 6. Asset Management (`asset.c`, `asset.h`)

EOS provides lightweight asset management (`eos_asset_t`) for accessing compiled-in static assets (such as fonts, images, configuration payloads, and executable scripts like `init.sh`).

---

## 7. Dynamic ELF Loader Subsystem (`elf.c`, `elf.h`, `elf_symbols.c`)

EOS integrates Espressif's relocatable ELF loader (`espressif/elf_loader`) to enable executing standalone user-space binaries compiled independently of the firmware:

* **ELF Format Detection:** `eos_is_elf_stream()` and `eos_is_elf_file()` detect the standard 4-byte ELF identifier (`0x7f, 'E', 'L', 'F'`).
* **Kernel Symbol Registry:** `eos_elf_init_symbols()` publishes kernel and runtime symbols (POSIX VFS, standard libc, string formatting, dynamic memory, and SDL 1.2/2.0 graphics) for dynamic symbol resolution via `esp_elf_register_symbol()`.
* **Execution & Relocation Pipeline:** `eos_elf_exec()` reads the ELF binary into a 4-byte aligned PSRAM buffer, relocates symbols with `esp_elf_relocate()`, synchronizes CPU cache lines via `esp_cache_msync()`, and executes the application entry point.
* **Process Model Integration:** The process execution pipeline (`main/ewrap/syscalls.c` and `main/ecore/exec.c`) transparently routes ELF executables to `eos_elf_stage_entry()` with 32 KB PSRAM thread stacks, preserving shell redirection (`<`, `>`, `>>`), pipes (`|`), and exit status (`$?`). For full details, see [Dynamic ELF Applications](../binaries/elf-applications.md).
