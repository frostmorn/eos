# EOS (ESP Operating System) Development Guide

EOS is a lightweight RTOS/OS abstraction layer for ESP32-S3 implementing a **Unified Device Model** (Everything is a file via `/dev` VFS).

---

## 1. Quick Start & Build Commands

*   **ESP-IDF Setup:** `source /opt/esp-idf/export.sh`
*   **Select Board:** `make setboard` (or `python tools/setboard.py esp32s3_qemu`)
*   **Build / Reconfigure / Clean:** `make build`, `make reconfigure`, `make clean`
*   **Flash / Monitor / QEMU / Debug:** `make flash`, `make monitor`, `make qemu`, `make debug`
*   **Code Generation:** `make driver` (`edriver`), `make bin` (`ebin`) via `tools/codegen.py`
*   **Audit & Analysis:** `make symbols`, `python tools/layer_audit.py`

---

## 2. Core Development & Design Rules

1.  **Planning Mode:** Use planning mode (`enter_plan_mode`) for designing and drafting new features before implementation.
2.  **Mandatory Testing:** Every new feature or bug fix requires corresponding automated tests in `tools/hardware_test.py`.
3.  **KISS & Occam's Razor:** Keep solutions simple, direct, and free of over-engineering or redundant fallback paths. Prioritize code reuse via `emisc/`.
4.  **Universal & Iterative Approach:** Prefer universal, robust abstractions and adopt an iterative refinement process over monolithic changes.

---

## 3. Layering & Architecture Principles

*   **`emisc/`**: Pure math, protocol algorithms, and reusable utilities (`kvec.h`, `netmisc.h`) with zero OS/hardware dependencies.
*   **`edriver/`**: Low-level hardware drivers encapsulating vendor SDK calls, interrupts, RTOS primitives, and working buffers inside `dev->state`. **Must use EOS logging (`EOS_LOGI`, etc.) instead of raw `printf`/`puts`**.
*   **`ecore/`**: Core kernel subsystems (VFS, devfs, binfs, pipefs, procfs, driver model).
*   **`ebin/`**: User-space commands and apps. **Must use POSIX / standard C interfaces (`usleep`, `pthread`, VFS file I/O)**. **Strictly forbidden:** RTOS primitives and global/static variables.
*   **`eboard/`**: Board-specific pin mappings and hardware startup.
*   **`eld/`**: Linker and link-fragment scripts (`.ld`, `.lf`). **All linker scripts must reside exclusively within `main/eld/`**.
*   **Header Guards:** Every header file (`.h`) must include `#pragma once`.

---

## 4. Memory & Buffer Management Rules

*   **Driver State Encapsulation:** All working buffers must reside in `dev->state`.
*   **No Global Variables:** User-space binaries (`ebin`) must remain stateless and reentrant.
*   **PSRAM Allocation:** Bulk data (framebuffers, packet capture buffers) must be allocated in PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`).
*   **Dynamic Arrays:** Prefer `kvec` (`emisc/kvec.h`) for safe array collections.
