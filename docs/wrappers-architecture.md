# POSIX, FreeRTOS & ESP-IDF Wrappers Architecture

EOS utilizes Linker-wrapped functions (`-Wl,--wrap=...` in `main/CMakeLists.txt`) to intercept standard Newlib, POSIX, and ESP-IDF system calls, injecting thread-context awareness, path normalization, and resource leak tracking transparently.

---

## 1. Memory Allocation Tracking (`ewrap/heap_caps.c`)

*   **Core Interception:** Wraps `__wrap_heap_caps_aligned_alloc_base` and `__wrap_heap_caps_free`.
*   **Automatic Leak Tracking:** Every allocation (`malloc`, `calloc`, `realloc`, `heap_caps_malloc`) is automatically registered in the calling thread's `eos_tctx_t` block vector (`memblocks`).
*   **Leak Cleanup on Exit:** When a thread terminates, any un-freed memory blocks are automatically released and logged as leaked memory, preventing heap fragmentation.

---

## 2. Reentrant Filesystem & Path Wrappers (`ewrap/reent_syscalls.c`, `ewrap/realpath.c`)

*   **Path Resolution:** Intercepts Newlib reentrant file operations (`_open_r`, `_stat_r`, `_link_r`, `_unlink_r`, `_rename_r`), `opendir`, `mkdir`, and `chdir`.
*   **Automatic Path Translation:** Relative paths passed to syscalls are automatically resolved against the calling thread's CWD via `eos_fapi_path_resolve`.
*   **Descriptor Tracking:** Opened file descriptors (`_open_r`) and directory pointers (`opendir`) are automatically registered to the thread context for leak detection and cleanup on thread exit.

---

## 3. Thread Creation Wrappers (`ewrap/pthread.c`, `ewrap/freertos_tasks.c`)

*   **Context Inheritance:** Intercepts `pthread_create` (`__wrap_pthread_create`) to prepare thread wrapper data (`eos_twrap_prepare`), ensuring newly spawned threads inherit the parent thread's CWD and thread context infrastructure.

---

## 4. SDL 1.2 & SDL 2.0 Compatibility Layers (`ewrap/sdl.c`, `ewrap/sdl2.c`)

EOS provides lightweight built-in compatibility layers mimicking SDL 1.2 and SDL 2.0 APIs on top of EOS framebuffer (`/dev/fb0`) and input (`/dev/keyboard0`) VFS devices.
*   **Video Surface & Rendering:** Translates `SDL_SetVideoMode`, `SDL_UpdateRect`, `SDL_CreateTexture`, and `SDL_RenderPresent` calls directly into VFS `ioctl` window dimension queries and framebuffer `write()` streams.
*   **Event Polling:** Translates `SDL_PollEvent` by polling input events from `/dev/keyboard0` via `EOS_KEYBOARD_IOCTL_POLL`.

---
[Back to Documentation Index](./Readme.md)
