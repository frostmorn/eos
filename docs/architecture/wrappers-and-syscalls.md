# POSIX, FreeRTOS & System Call Wrappers Architecture

EOS utilizes Linker-wrapped functions (`-Wl,--wrap=...` in `main/CMakeLists.txt`) to intercept standard Newlib, POSIX, and ESP-IDF system calls, injecting thread-context awareness, path normalization, resource leak tracking, and compatibility layers transparently.

---

## 1. Memory Allocation Tracking (`ewrap/heap_caps.c`)

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
