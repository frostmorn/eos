# EOS (ESP Operating System) Development Guide

EOS is an abstraction layer and lightweight operating system for the ESP32 (specifically targeting ESP32-S3) that implements a **Unified Device Model**. It transforms heterogeneous hardware peripherals into a consistent, hierarchical **Device Tree** accessible via a standard **Virtual Filesystem (VFS)** interface (`/dev`), adhering to the "Everything is a file" philosophy.

---

## 1. Environment Setup & Prerequisites

Before building or flashing EOS, you must initialize the **ESP-IDF** environment variables in your terminal shell.

```bash
source /opt/esp-idf/export.sh
```

*(Note: Ensure your ESP-IDF path matches your local installation if it differs from `/opt/esp-idf/export.sh`).*

---

## 2. Selecting a Target Board

EOS supports multiple target hardware boards (located in `main/eboard/`). Before building, select your board configuration:

```bash
make setboard
```
This runs `python tools/setboard.py`, which lists available boards interactively and copies the appropriate `sdkconfig` to the project root directory.

*(Tip: You can also select a board non-interactively via command line: `python tools/setboard.py esp32s3_qemu`).*

---

## 3. Build System & Compilation

The project uses a standard `Makefile` wrapper around ESP-IDF's `idf.py` build system.

### Common Build Commands

*   **Build Firmware:**
    ```bash
    make build
    ```
    *(Equivalent to `idf.py build`, compiles the project and generates `build/eos.elf`).*

*   **Reconfigure Project:**
    ```bash
    make reconfigure
    ```

*   **Configure via Menuconfig:**
    ```bash
    make menuconfig
    ```

*   **Clean Build Artifacts:**
    ```bash
    make clean
    ```
    *(Runs `idf.py fullclean`).*

---

## 4. Flashing, Monitoring & Debugging

*   **Flash Firmware to Device:**
    ```bash
    make flash
    ```

*   **Open Serial Monitor:**
    ```bash
    make monitor
    ```

*   **Run in QEMU Emulator:**
    ```bash
    make qemu
    ```
    *(Launches EOS in QEMU via `idf.py qemu` / `qemu-system-xtensa`).*
    *(Note: `make qemu` requires Espressif's custom QEMU (`qemu-xtensa`) with ESP32-S3 machine support (`-M esp32s3`). If you encounter an `unsupported machine type: "esp32s3"` error when using system QEMU, install the IDF-provided QEMU tool by running:*
    ```bash
    python $IDF_PATH/tools/idf_tools.py install qemu-xtensa
    ```
    *)*

*   **Launch JTAG Debugger (GDB + OpenOCD):**
    ```bash
    make debug
    ```
    *(Launches OpenOCD server and connects `xtensa-esp32s3-elf-gdb` with TUI support).*

*   **Kill OpenOCD Server:**
    ```bash
    make killdebug
    ```

---

## 5. Code Generation Tools

EOS includes built-in interactive template generators (`tools/codegen.py`) for creating new drivers and binaries/apps.

*   **Generate a New Driver (`edriver`):**
    ```bash
    make driver
    ```
    *(Prompts for driver parameters and generates template files under `main/edriver`).*

*   **Generate a New Binary / Application (`ebin`):**
    ```bash
    make bin
    ```
    *(Prompts for binary parameters and generates template files under `main/ebin`).*

---

## 6. Analysis & Utility Commands

*   **List Firmware Symbols:**
    ```bash
    make symbols
    ```

*   **Disassemble Firmware ELF:**
    ```bash
    make disasm
    ```

*   **Static Code Analysis (Cppcheck):**
    ```bash
    make cppcheck
    ```

*   **Scan TODOs and Research Tags:**
    ```bash
    make todo
    make research
    ```

---

## 7. Project Architecture Overview

*   **`main/`**: Core OS implementation
    *   **`ecore/`**: Core kernel subsystems (VFS, device management, binfs, devfs, capsmgr, driver model, thread context, tmpfs).
    *   **`edriver/`**: Hardware drivers categorized by scope (`bus/`, `display/`, `sound/`, `storage/`).
    *   **`ebin/`**: Built-in system commands and applications (`ush`, `ls`, `cat`, `mkdir`, `mount`, test utilities, etc.).
    *   **`eboard/`**: Board-specific definitions and configurations (`lilka_v23`, etc.).
    *   **`eld/`**: Linker scripts for compile-time driver and binary registration.
    *   **`ewrap/`**: ESP-IDF / FreeRTOS / POSIX syscall wrappers and hooks.
*   **`components/`**: External components such as LVGL graphics library.
*   **`docs/`**: Detailed architecture scope documents (Bus, Display, Sound, Storage, VFS).
*   **`tools/`**: Build setup and code generation scripts (`setboard.py`, `codegen.py`, debug scripts).
