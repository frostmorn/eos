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
| `_open_r` | `main/ewrap/reent_syscalls.c` | Resolves relative paths via calling thread's CWD; tracks open file descriptors in thread context for auto-cleanup. | Newlib `open()` routes internally to `_open_r(struct _reent *, ...)`. | `tools/hardware_test.py --suite coreutils` |
| `_close_r` | `main/ewrap/reent_syscalls.c` | Untracks closed file descriptor from thread context; falls back to `__real__close_r`. | Standard Newlib reentrant close handler. | `tools/hardware_test.py --suite coreutils` |
| `_read_r` | `main/ewrap/reent_syscalls.c` | Maps redirected file descriptors (`fd_map[0]` stdin redirection); resolves pipe fds; delivers data. | ESP-IDF console forwards reads to primary UART; redirects must intercept reentrant read. | `tools/hardware_test.py --suite coreutils` (pipelines) |
| `_write_r` | `main/ewrap/reent_syscalls.c` | Maps redirected stdout/stderr descriptors (`fd_map[1]`, `fd_map[2]`); bypasses stdout recursion for raw console uart fd 0. | Standard Newlib reentrant write; ESP-IDF `console_write` writes to primary fd 0. | `tools/hardware_test.py --suite coreutils` |
| `_lseek_r` | `main/ewrap/reent_syscalls.c` | Resolves thread descriptor map and performs seek. | Newlib `lseek` routes to `_lseek_r`. | `tools/hardware_test.py --suite coreutils` |
| `_fstat_r` | `main/ewrap/reent_syscalls.c` | Resolves thread descriptor map, synthesizes stat attributes for pipes and virtual devices. | Newlib reentrant fstat handler. | `tools/hardware_test.py --suite coreutils` |
| `_stat_r` | `main/ewrap/reent_syscalls.c` | Normalizes path via CWD and stats filesystem nodes or pseudo files. | Newlib reentrant stat handler. | `tools/hardware_test.py --suite coreutils` |
| `_link_r` | `main/ewrap/reent_syscalls.c` | Normalizes source/target paths; enforces link permissions. | Newlib reentrant hard-link handler. | `tools/hardware_test.py --suite coreutils` |
| `_unlink_r` | `main/ewrap/reent_syscalls.c` | Normalizes path and unlinks files. | Newlib reentrant unlink handler. | `tools/hardware_test.py --suite coreutils` |
| `_rename_r` | `main/ewrap/reent_syscalls.c` | Normalizes source/destination paths across VFS nodes. | Newlib reentrant rename handler. | `tools/hardware_test.py --suite coreutils` |
| `opendir` | `main/ewrap/reent_syscalls.c` | Normalizes directory path via CWD and registers `DIR*` handle for auto-close tracking. | Standard POSIX directory open. | `tools/hardware_test.py --suite coreutils` |
| `closedir` | `main/ewrap/reent_syscalls.c` | Untracks directory handle and invokes underlying close. | Standard POSIX directory close. | `tools/hardware_test.py --suite coreutils` |
| `mkdir` | `main/ewrap/reent_syscalls.c` | Normalizes directory path via thread CWD. | Standard POSIX directory creation. | `tools/hardware_test.py --suite coreutils` |
| `chdir` | `main/ewrap/realpath.c` | Updates thread-isolated current working directory in thread context (`tctx`). | POSIX directory change. | `tools/hardware_test.py --suite coreutils` |
| `getcwd` | `main/ewrap/realpath.c` | Retrieves thread-isolated current working directory. | POSIX CWD retrieval. | `tools/hardware_test.py --suite coreutils` |
| `system` | `main/ewrap/syscalls.c` | Intercepts shell execution, parses commands/pipelines/redirections, executes native ebins and scripts. | Standard C library command execution entrypoint. | `tools/hardware_test.py --suite coreutils` |
| `usleep` | `main/ewrap/usleep.c` | High-precision microsecond sleep integrating with FreeRTOS delay ticks. | Standard POSIX microsecond sleep. | `tools/hardware_test.py --suite coreutils` |
| `pthread_create` | `main/ewrap/pthread.c` | Injects thread wrapper (`eos_twrap`), thread context inheritance, and manifest PSRAM stack attributes. | ESP-IDF pthread implementation. | `main/ebin/test_sdl`, GUI ebin launches |
| `xTaskCreatePinnedToCore` | `main/ewrap/freertos_tasks.c` | Captures FreeRTOS tasks spawned dynamically, initializing EOS thread context tracking. | FreeRTOS task creation primitive. | Thread tracking & cleanup tests |
| `xTaskCreateStaticPinnedToCore` | `main/ewrap/freertos_tasks.c` | Captures statically allocated FreeRTOS tasks to attach EOS thread contexts. | FreeRTOS static task creation primitive. | Thread tracking & cleanup tests |
| `heap_caps_aligned_alloc_base` | `main/ewrap/heap_caps.c` | Tracks dynamic memory allocations per thread/task in `memblocks` for leak prevention. | Core ESP-IDF heap allocator backend for all malloc calls. | `tools/hardware_test.py --suite emisc` |
| `heap_caps_realloc_base` | `main/ewrap/heap_caps.c` | Updates tracked allocation pointers upon reallocation. | Core ESP-IDF heap reallocator backend. | `tools/hardware_test.py --suite emisc` |
| `heap_caps_free` | `main/ewrap/heap_caps.c` | Removes tracked memory block and frees underlying heap memory. | Core ESP-IDF heap free backend. | `tools/hardware_test.py --suite emisc` |

---

## 2. Memory Allocation Tracking (`ewrap/heap_caps.c`)

EOS wraps Espressif's underlying heap allocators:
* `__wrap_heap_caps_aligned_alloc_base`
* `__wrap_heap_caps_free`
* `__wrap_heap_caps_realloc_base`

### Automatic Leak Detection & Cleanup:
1. **Per-Thread Registration:** Every dynamic memory allocation (`malloc`, `calloc`, `realloc`, `heap_caps_malloc`) is registered in the calling thread's `eos_tctx_t` tracking vector (`memblocks`).
2. **Reallocation Safety:** `__wrap_heap_caps_realloc_base` tracks pointer address updates, preventing double-free or dangling pointer bugs.
3. **Automatic Cleanup on Exit:** When any thread (pthread or FreeRTOS task) terminates, EOS automatically frees all unreleased memory blocks and emits a diagnostic warning log (`EOS_LOGW`), guaranteeing long-term system stability without memory leaks.

---

## 2. Reentrant Filesystem & Path Wrappers (`ewrap/reent_syscalls.c`, `ewrap/realpath.c`)

Newlib reentrant filesystem calls are intercepted to provide multi-process path isolation:
* Intercepts: `_open_r`, `_stat_r`, `_link_r`, `_unlink_r`, `_rename_r`, `opendir`, `mkdir`, and `chdir`.
* **Automatic Path Resolution:** Relative paths (e.g. `file.txt`, `../config.ini`) are automatically normalized and resolved against the calling thread's isolated CWD via `eos_fapi_path_resolve()`.
* **File Descriptor & Directory Tracking:** File descriptors opened via `_open_r` and directory handles from `opendir` are registered to the thread context. If a command crashes or returns without closing them, the kernel closes them automatically upon thread termination.

---

## 3. Thread Creation & Stack Routing (`ewrap/pthread.c`, `ewrap/syscalls.c`)

* **Context Inheritance:** `__wrap_pthread_create()` intercepts thread creation, allocating and initializing thread wrapper data (`eos_twrap_prepare`). Child threads seamlessly inherit the parent thread's CWD, file descriptor visibility, and environment.
* **Manifest Stack Preference Injection:** `eos_pthread_set_manifest_cfg(bin)` configures ESP-IDF pthread attributes, routing GUI applications (`EOS_BIN_FLAG_GUI`) to 16 KB external PSRAM stacks.
* **Graceful SRAM Fallback:** If PSRAM stack allocation fails (`rc != 0`), the wrapper logs a warning and falls back to internal SRAM.
* **Command Dispatch in `__wrap_system()`:** When executing single-stage commands (`stage_count == 1`), `syscalls.c` inspects `eos_bin_prefers_psram_stack(bin)`. If the target binary requires a PSRAM stack (such as `launcher` or `lvgl_net`), it spawns a dedicated joinable pthread rather than executing on the caller's stack, preventing stack overflows on RISC-V architectures.

---

## 4. SDL 1.2 & SDL 2.0 Compatibility Layers (`ewrap/sdl.c`, `ewrap/sdl2.c`)

EOS provides built-in compatibility layers that emulate SDL 1.2 and SDL 2.0 APIs on top of EOS VFS devices:

* **Video Surface & Framebuffer Blitting:** Translates `SDL_SetVideoMode`, `SDL_UpdateRect`, `SDL_CreateWindow`, `SDL_CreateRenderer`, and `SDL_RenderPresent` directly into `/dev/fb0` window IOCTLs and raw RGB565 `write()` streams.
* **Input Event Translation:** Translates `SDL_PollEvent` by non-blocking polling `/dev/keyboard0` via `EOS_KEYBOARD_IOCTL_POLL`, mapping EOS input keycodes directly into `SDLK_*` events.
