# POSIX, FreeRTOS & System Call Wrappers Architecture

EOS utilizes Linker-wrapped functions (`-Wl,--wrap=...` in `main/CMakeLists.txt`) to intercept standard Newlib, POSIX, and ESP-IDF system calls, injecting thread-context awareness, path normalization, resource leak tracking, and compatibility layers transparently.

---

## 0. Supported Target Matrix & Toolchain Pinning

EOS supports and validates the following target architectures and runtime environments on **ESP-IDF v6.0.2**:

| Target Name | SoC Family | Architecture | Key Characteristics & Memory | Verified Environments |
|---|---|---|---|---|
| `lilka_v23` | ESP32-S3 | Xtensa dual-core LX7 | Octal PSRAM (8 MB), ST7789 display, I2S sound, SD storage | Real hardware |
| `jc4880p443c_i_w_y` | ESP32-P4 | RISC-V dual-core HP | High-performance, MIPI-DSI/ST7701S, ESP-Hosted SDIO co-processor | Real hardware |
| `esp32s3_qemu` | ESP32-S3 | Xtensa dual-core LX7 | Emulated S3, 32 MB flash, Octal PSRAM, OpenEth / UART | QEMU emulator & CI |

---

## 1. Linker Wrappers Inventory & Verification Matrix

Every linker-wrapped symbol (`-Wl,--wrap=<sym>`) configured in [`main/CMakeLists.txt`](../../main/CMakeLists.txt) must be documented in this inventory table. This ensures full visibility into toolchain dependencies, underlying Newlib/ESP-IDF assumptions, and the test suites that guard each wrapper against silent breakage.

| Wrapped Symbol | Implementation File | Purpose & Architectural Role | Newlib / ESP-IDF Assumption | Test Suite Guard |
|---|---|---|---|---|
| `_open_r` | `main/ewrap/reent_syscalls.c` | Resolves relative paths via calling process's CWD; tracks open file descriptors in process context (`eos_proc_t`) for auto-cleanup. | Newlib `open()` routes internally to `_open_r(struct _reent *, ...)`. | `tools/hardware_test.py --suite coreutils` |
| `_close_r` | `main/ewrap/reent_syscalls.c` | Untracks closed file descriptor from process context; falls back to `__real__close_r`. | Standard Newlib reentrant close handler. | `tools/hardware_test.py --suite coreutils` |
| `_read_r` | `main/ewrap/reent_syscalls.c` | Maps redirected file descriptors (`fd_map[0]` stdin redirection); resolves pipe fds; delivers data. | ESP-IDF console forwards reads to primary UART; redirects must intercept reentrant read. | `tools/hardware_test.py --suite coreutils` (pipelines) |
| `_write_r` | `main/ewrap/reent_syscalls.c` | Maps redirected stdout/stderr descriptors (`fd_map[1]`, `fd_map[2]`); bypasses stdout recursion for raw console uart fd 0. | Standard Newlib reentrant write; ESP-IDF `console_write` writes to primary fd 0. | `tools/hardware_test.py --suite coreutils` |
| `_lseek_r` | `main/ewrap/reent_syscalls.c` | Resolves process descriptor map and performs seek. | Newlib `lseek` routes to `_lseek_r`. | `tools/hardware_test.py --suite coreutils` |
| `_fstat_r` | `main/ewrap/reent_syscalls.c` | Resolves process descriptor map, synthesizes stat attributes for pipes and virtual devices. | Newlib reentrant fstat handler. | `tools/hardware_test.py --suite coreutils` |
| `_stat_r` | `main/ewrap/reent_syscalls.c` | Normalizes path via process CWD and stats filesystem nodes or pseudo files. | Newlib reentrant stat handler. | `tools/hardware_test.py --suite coreutils` |
| `_link_r` | `main/ewrap/reent_syscalls.c` | Normalizes source/target paths; enforces link permissions. | Newlib reentrant hard-link handler. | `tools/hardware_test.py --suite coreutils` |
| `_unlink_r` | `main/ewrap/reent_syscalls.c` | Normalizes path and unlinks files. | Newlib reentrant unlink handler. | `tools/hardware_test.py --suite coreutils` |
| `_rename_r` | `main/ewrap/reent_syscalls.c` | Normalizes source/destination paths across VFS nodes. | Newlib reentrant rename handler. | `tools/hardware_test.py --suite coreutils` |
| `opendir` | `main/ewrap/reent_syscalls.c` | Normalizes directory path via process CWD and registers `DIR*` handle for auto-close tracking. | Standard POSIX directory open. | `tools/hardware_test.py --suite coreutils` |
| `readdir` | `main/ewrap/reent_syscalls.c` | Serializes directory traversal using per-stream descriptor lock and disables heap tracking. | Standard POSIX directory entry read. | `tools/hardware_test.py --suite coreutils` |
| `closedir` | `main/ewrap/reent_syscalls.c` | Untracks directory handle from process context and invokes underlying close. | Standard POSIX directory close. | `tools/hardware_test.py --suite coreutils` |
| `mkdir` | `main/ewrap/reent_syscalls.c` | Normalizes directory path via process CWD. | Standard POSIX directory creation. | `tools/hardware_test.py --suite coreutils` |
| `chdir` | `main/ewrap/realpath.c` | Updates process Current Working Directory in process context (`eos_proc_t`). | POSIX directory change. | `tools/hardware_test.py --suite coreutils` |
| `getcwd` | `main/ewrap/realpath.c` | Retrieves process Current Working Directory from process context (`eos_proc_t`). | POSIX CWD retrieval. | `tools/hardware_test.py --suite coreutils` |
| `system` | `main/ewrap/syscalls.c` | Intercepts shell execution, parses commands/pipelines/redirections, executes native ebins and scripts. | Standard C library command execution entrypoint. | `tools/hardware_test.py --suite coreutils` |
| `usleep` | `main/ewrap/usleep.c` | High-precision microsecond sleep integrating with FreeRTOS delay ticks. | Standard POSIX microsecond sleep. | `tools/hardware_test.py --suite coreutils` |
| `getpid` | `main/ewrap/syscalls.c` | Returns POSIX process ID from calling thread's process context or PID 1 init. | POSIX process ID query; replaces Newlib dummy stub. | `tools/hardware_test.py --suite proc` |
| `pthread_create` | `main/ewrap/pthread.c` | Injects thread wrapper (`eos_twrap`), process context inheritance, and manifest PSRAM stack attributes. | ESP-IDF pthread implementation. | `main/ebin/test_sdl`, GUI ebin launches |
| `xTaskCreatePinnedToCore` | `main/ewrap/freertos_tasks.c` | Captures FreeRTOS tasks spawned dynamically, initializing EOS process context association. | FreeRTOS task creation primitive. | Thread tracking & cleanup tests |
| `xTaskCreateStaticPinnedToCore` | `main/ewrap/freertos_tasks.c` | Captures statically allocated FreeRTOS tasks to attach EOS process context. | FreeRTOS static task creation primitive. | Thread tracking & cleanup tests |
| `heap_caps_malloc_base` | `main/ewrap/heap_caps.c` | Tracks dynamic memory allocations per process in `memblocks` for leak prevention. | Core ESP-IDF heap allocator backend for malloc calls. | `tools/hardware_test.py --suite proc` |
| `heap_caps_calloc_base` | `main/ewrap/heap_caps.c` | Tracks dynamic memory allocations per process in `memblocks` for leak prevention. | Core ESP-IDF heap allocator backend for calloc calls. | `tools/hardware_test.py --suite proc` |
| `heap_caps_aligned_alloc_base` | `main/ewrap/heap_caps.c` | Tracks dynamic memory allocations per process in `memblocks` for leak prevention. | Core ESP-IDF heap allocator backend for all aligned malloc calls. | `tools/hardware_test.py --suite emisc` |
| `heap_caps_realloc_base` | `main/ewrap/heap_caps.c` | Updates tracked allocation pointers in process context upon reallocation. | Core ESP-IDF heap reallocator backend. | `tools/hardware_test.py --suite emisc` |
| `heap_caps_free` | `main/ewrap/heap_caps.c` | Removes tracked memory block from process context and frees underlying heap memory. | Core ESP-IDF heap free backend. | `tools/hardware_test.py --suite emisc` |
| `usb_serial_jtag_is_connected` | `main/ewrap/reent_syscalls.c` | Overrides ESP-IDF USB Serial/JTAG SOF monitor to keep console VFS active across JTAG resets. | ESP-IDF USB Serial/JTAG connection monitor. | Hardware testing on ESP32-S3 / ESP32-P4 |

---

## 2. Memory Allocation Tracking (`ewrap/heap_caps.c`)

EOS wraps Espressif's underlying heap allocators:
* `__wrap_heap_caps_malloc_base`
* `__wrap_heap_caps_calloc_base`
* `__wrap_heap_caps_aligned_alloc_base`
* `__wrap_heap_caps_free`
* `__wrap_heap_caps_realloc_base`

### Automatic Leak Detection & Cleanup:
1. **Per-Process Registration:** Every dynamic memory allocation (`malloc`, `calloc`, `realloc`, `heap_caps_malloc`) is registered in the calling process's `eos_proc_t` tracking vector (`memblocks`, protected by `mem_mutex`). Sibling threads of the same process share this allocation list.
2. **Reallocation Safety:** `__wrap_heap_caps_realloc_base` tracks pointer address updates, preventing double-free or dangling pointer bugs.
3. **Automatic Cleanup on Exit / Reaping:** When all threads of a process terminate and the process is reaped via `waitpid()`, EOS automatically frees all unreleased memory blocks and closes leaked descriptors/directories, guaranteeing long-term system stability without memory leaks.

---

## 3. Reentrant Filesystem & Path Wrappers (`ewrap/reent_syscalls.c`, `ewrap/realpath.c`)

Newlib reentrant filesystem calls are intercepted to provide multi-process path isolation:
* Intercepts: `_open_r`, `_stat_r`, `_link_r`, `_unlink_r`, `_rename_r`, `opendir`, `mkdir`, and `chdir`.
* **Automatic Path Resolution:** Relative paths (e.g. `file.txt`, `../config.ini`) are automatically normalized and resolved against the calling process's isolated CWD via `eos_fapi_path_resolve()`.
* **File Descriptor & Directory Tracking:** File descriptors opened via `_open_r` and directory handles from `opendir` are registered to the process context (`proc->fds`, `proc->dirs`). When a command crashes or exits without closing them, the kernel closes them automatically upon process reaping.

---

## 4. Thread Creation & Stack Routing (`ewrap/pthread.c`, `ewrap/syscalls.c`)

* **Context Inheritance:** `__wrap_pthread_create()` intercepts thread creation, allocating and initializing thread wrapper data (`eos_twrap_prepare`). Child threads seamlessly inherit the parent process's context (`eos_proc_t`), CWD, file descriptor visibility, and environment.
* **Manifest Stack Preference Injection:** `eos_pthread_set_manifest_cfg(bin)` configures ESP-IDF pthread attributes, routing GUI applications (`EOS_BIN_FLAG_GUI`) to 16 KB external PSRAM stacks.
* **Graceful SRAM Fallback:** If PSRAM stack allocation fails (`rc != 0`), the wrapper logs a warning and falls back to internal SRAM.
* **Command Dispatch in `__wrap_system()`:** When executing single-stage commands (`stage_count == 1`), `syscalls.c` inspects `eos_bin_prefers_psram_stack(bin)`. If the target binary requires a PSRAM stack (such as `launcher` or `lvgl_net`), it spawns a dedicated joinable pthread rather than executing on the caller's stack, preventing stack overflows on RISC-V architectures.

---

## 5. SDL 1.2 & SDL 2.0 Compatibility Layers (`ewrap/sdl.c`, `ewrap/sdl2.c`)

EOS provides built-in compatibility layers that emulate SDL 1.2 and SDL 2.0 APIs on top of EOS VFS devices:

* **Video Surface & Framebuffer Blitting:** Translates `SDL_SetVideoMode`, `SDL_UpdateRect`, `SDL_CreateWindow`, `SDL_CreateRenderer`, and `SDL_RenderPresent` directly into `/dev/fb0` window IOCTLs and raw RGB565 `write()` streams.
* **Input Event Translation:** Translates `SDL_PollEvent` by non-blocking polling `/dev/keyboard0` via `EOS_KEYBOARD_IOCTL_POLL`, mapping EOS input keycodes directly into `SDLK_*` events.

---

## 6. Early-Boot, System Tasks & Memory Tracking Invariants (`ewrap/heap_caps.c`)

To guarantee absolute memory isolation, prevent memory tracking recursion, and avoid heap corruption, heap tracking must adhere to strict rules:

### Allocation Tracking Gate Conditions
Heap allocation tracking into `proc->memblocks` occurs **only** when all of the following conditions are met:
1. `xTaskGetSchedulerState() == taskSCHEDULER_RUNNING` (FreeRTOS scheduler is active).
2. `!xPortInIsrContext()` (Not executing within an interrupt service routine).
3. `!is_system_task()`: Excludes early system tasks (`"main"`, `"IDLE"`, `"ipc*"`, `"sdio_*"`, `"rpc_*"`, `"Tmr Svc"`, `"sys_evt"`, `"wifi"`).
4. `!eos_proc_in_heap_wrap()`: Thread is not already inside a heap wrapper call (reentrancy lock).
5. `!eos_proc_heap_tracking_is_disabled()`: Internal kernel operations (such as `kvec` table resizes) explicitly disable tracking via `eos_proc_heap_tracking_disable()`.
6. `proc != NULL`: An active process context exists for the calling thread.

### Pre-Scheduler & Kernel Process Isolation
- During early boot before `app_main` initializes the process subsystem, allocations directly route to `__real_heap_caps_*` with zero tracking overhead.
- Allocations made by the kernel (`s_kernel_proc`, PID 0) are stored in `s_kernel_proc.memblocks` and are **never reaped** by `waitpid()`. When driver state or memory is handed over across processes, `eos_mem_adopt(ptr, target_pid)` safely migrates the ownership record without double-freeing or leaking.

---

## 7. FreeRTOS & POSIX Synchronization Primitives Lifecycle

In ESP-IDF, POSIX `pthread_mutex_t` and `pthread_cond_t` lazily allocate memory dynamically on their first use when initialized with `PTHREAD_MUTEX_INITIALIZER` or `PTHREAD_COND_INITIALIZER`:
- **The Lazy Allocation Pitfall:** If a static kernel mutex is first locked from within a child process context, the internal `esp_pthread_mutex_t` structure is allocated via `malloc()` and tracked into that child's `memblocks`. When the child process terminates and is reaped via `waitpid()`, the kernel mutex is destroyed and its memory freed, causing subsequent lock operations on other threads to crash on corrupted memory.
- **The Upfront Initialization Rule:** All kernel-level mutexes and condition variables (`pipe_table_lock`, `s_proc_table_cond`, `s_proc_mutex`, process slot `mem_mutex`, etc.) **must be initialized explicitly during subsystem boot** (`eos_pipefs_init()`, `eos_proc_init()`) before any user processes execute. No static kernel synchronization primitive may rely on lazy runtime initialization.

---

## 8. Unified VFS & Upper-Level Locking Architecture

EOS enforces centralized synchronization at the Virtual File System (VFS) descriptor level (`main/ecore/vfs.c`, `main/ecore/vfs.h`):
- Open file descriptors (`eos_vfs_fd_desc_t`) and directories (`eos_vfs_dir_desc_t`) are first-class kernel objects containing dedicated recursive mutexes (`lock`).
- High-level VFS entrypoints (`read`, `write`, `lseek`, `fstat`, `readdir`, `closedir`) acquire the per-descriptor mutex before invoking the underlying filesystem driver vtable.
- This upper-level locking ensures that filesystem drivers (`pipefs`, `devfs`, `procfs`, `tmpfs`) do not require intricate internal locks for routine I/O operations, completely eliminating multi-thread deadlocks, race conditions on shared ring-buffers, and descriptor reuse hazards.
- All descriptor allocations and mutex initializations occur statically upfront during boot (`eos_vfs_init()`), avoiding runtime POSIX emulation heap allocations within user contexts.
- Driver internal bookkeeping during `open()`, `close()`, `opendir()`, and `closedir()` is performed under `eos_proc_heap_tracking_disable()`, ensuring driver internal buffers are never recorded into user process memory tables.
