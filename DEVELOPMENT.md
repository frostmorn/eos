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

*   **Refine Documentation:**
    ```bash
    make refine-docs
    ```
    *(Invokes Gemini to refine project documentation based on `DEVELOPMENT.md`).*

---

## 7. Project Architecture Overview & Layered Design Principles

EOS enforces a strict **Layered Architecture** to guarantee clean separation of concerns, reusability, and maintainability:

*   **`main/`**: Core OS implementation
    *   **`emisc/`**: Math, utilities, protocol formatting, and reusable algorithms (e.g., PCAP formatting, CIDR/IP math, MAC parsing) that can be shared across drivers and user-space binaries.
    *   **`edriver/`**: Low-level hardware drivers categorized by scope (`bus/`, `display/`, `input/`, `net/`, `sound/`, `storage/`). Encapsulates hardware-specific callbacks, interrupts, and raw peripheral I/O.
    *   **`ecore/`**: Core kernel subsystems (VFS, device management, binfs, devfs, pipefs, procfs, capsmgr, driver model, thread context, tmpfs, asset management, net_common, ioctl definitions).
    *   **`ebin/`**: User-space system commands and applications (`ush`, `ls`, `cat`, `mkdir`, `mount`, network utilities `wlanctl`/`netctl`, UI apps, test utilities). Consumes kernel interfaces and low-level drivers via VFS and ioctl.
    *   **`eboard/`**: Board-specific definitions and configurations (`lilka_v23`, `esp32s3_qemu`, `jc4880p443c_i_w_y`).
    *   **`eld/`**: Linker scripts for compile-time driver and binary registration.
    *   **`ewrap/`**: ESP-IDF / FreeRTOS / POSIX syscall wrappers and hooks.
*   **`components/`**: External components such as LVGL graphics library.
*   **`docs/`**: Detailed architecture scope documents (Bus, Display, Input, Network, Sound, Storage, VFS, Wrappers, Board Porting).
*   **`tools/`**: Build setup and code generation scripts (`setboard.py`, `codegen.py`, debug scripts).

---

## 8. Project Memory & Custom Skills

### Memory Loading Rule
- **Explicit Rule:** Developers and AI agents must load and consult `.ai/MEMORY.md` at the start of tasks to retrieve project-specific private notes, architectural reminders, and workspace conventions.

### Custom Skills (`.ai/skills/`)
The repository includes workspace-scoped custom skills under `.ai/skills/`:
- **`qemu-testing`**: Located in `.ai/skills/qemu-testing/SKILL.md`. Provides specialized procedures for building firmware, running QEMU in the background (`esp32s3_qemu`), serial log redirection, and process cleanup.
- **Skill Loadage / Activation:** Activate workspace skills using the `activate_skill` tool (e.g., `activate_skill(name="qemu-testing")`) when performing corresponding specialized workflows.
