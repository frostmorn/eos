# EOS (ESP Operating System) Development Guide

EOS is a lightweight RTOS/OS abstraction layer for ESP32 microcontrollers (ESP32-S3 and ESP32-P4) implementing a **Unified Device Model** (Everything is a file via `/dev` VFS).

---

## 1. Quick Start & Build Commands

*   **ESP-IDF Setup:** `source /opt/esp-idf/export.sh`
*   **Select Board:** `make setboard` (or `python3 tools/setboard.py esp32s3_qemu`)
*   **Build / Reconfigure / Clean:** `make build`, `make reconfigure`, `make clean`
*   **Flash / Monitor / QEMU / Debug:** `make flash`, `make monitor`, `make qemu`, `make qemu_graphics`, `make debug`
*   **Code Generation:** `make driver` (`edriver`), `make bin` (`ebin`) via `tools/codegen.py`
*   **Audit & Analysis:** `make symbols`, `python3 tools/layer_audit.py`

---

## 2. Core Development & Design Rules

1.  **Planning Mode:** Use planning mode (`enter_plan_mode`) for designing and drafting new features before implementation.
2.  **Mandatory Testing:** Every new feature or bug fix requires corresponding automated tests in `tools/hardware_test.py`.
3.  **KISS & Occam's Razor:** Keep solutions simple, direct, and free of over-engineering or redundant fallback paths. Prioritize code reuse via `emisc/`.
4.  **Universal & Iterative Approach:** Prefer universal, robust abstractions and adopt an iterative refinement process over monolithic changes.

---

## 3. Layering & Architecture Principles

*   **`emisc/`**: Pure math, protocol algorithms, and reusable utilities (`kvec.h`, `netmisc.h`) with zero OS/hardware dependencies.
*   **`edriver/`**: Low-level hardware drivers encapsulating vendor SDK calls, interrupts, RTOS primitives, and working buffers inside `dev->state`.
    *   **Logging:** Must use EOS logging (`EOS_LOGI`, etc.) instead of raw `printf`/`puts`.
    *   **Build Guard:** All driver source files (`.c`) must be conditionally guarded by `EOS_DRV_*_ENABLED` preprocessor macros.
    *   **Driver Structure:** Every driver directory under `main/edriver/<scope>/<name>/` must define an `eos_drv_t` structure registered in the `.eos_drivers` section (`EOS_DRV_ATTR eos_drv_t driver_<scope>_<name> = { ... };`).
    *   **No Loose Driver Files:** No loose `.c` or `.h` files are permitted directly under `main/edriver/` or `main/edriver/<scope>/`.
    *   **IOCTL-Only Driver Communication:** Drivers must pass and consume functionality strictly via standard VFS and `ioctl` interfaces (`eos_dev_ioctl_call` on parent/child/peer devices) rather than loose C helper functions or ad-hoc cross-driver exports.
*   **`ecore/`**: Core kernel subsystems (VFS, devfs, binfs, pipefs, procfs, driver model).
*   **`ebin/`**: User-space commands and apps. **Must use POSIX / standard C interfaces (`usleep`, `pthread`, VFS file I/O)**. **Strictly forbidden:** RTOS primitives and global/static variables.
*   **`eboard/`**: Board configurations and declarative startup scripts (`init.sh`, `sdkconfig`). Peripheral attachments are declared via `devctl attach` rather than hardcoded C initialization files.
*   **`eld/`**: Linker and link-fragment scripts (`.ld`, `.lf`). **All linker scripts must reside exclusively within `main/eld/`**.
*   **`ewrap/`**: System call and library wrappers. **All `__wrap_*` wrapper function definitions must reside exclusively within `main/ewrap/`**.
*   **Header Guards:** Every header file (`.h`) must include `#pragma once`.

---

## 4. Memory & Buffer Management Rules

*   **Driver State Encapsulation:** All working buffers must reside in `dev->state`.
*   **No Global Variables:** User-space binaries (`ebin`) must remain stateless and reentrant.
*   **PSRAM Allocation:** Bulk data (framebuffers, packet capture buffers) must be allocated in PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`).
*   **Dynamic Arrays:** Prefer `kvec` (`emisc/kvec.h`) for safe array collections.

---

## 5. Hardware Testing, Flashing & OpenOCD JTAG Debugging

Physical targets (ESP32-P4 and ESP32-S3) connect via built-in USB-Serial-JTAG (`303a:1001`).

### Anti-Patterns to Avoid (Strictly Prohibited)
*   **NEVER perform blind DTR/RTS serial resetting or rely on arbitrary sleep delays (`time.sleep`)**:
    *   Toggling DTR pulls down the boot strapping pin (`GPIO35` on ESP32-P4, `GPIO0` on ESP32-S3).
    *   Releasing `RTS` while `DTR` is active latches `GPIO_STRAP_REG = 0x207` (**ROM Download Mode**), trapping the board in a bootloop where it never boots EOS and locks up the host CDC-ACM serial driver.
    *   Host serial write calls without non-blocking mode or timeouts lock up inside the Linux kernel `tty_write` when endpoints are unserviced.
*   **NEVER leave orphaned OpenOCD or GDB processes running in the background**:
    *   OpenOCD claims the USB JTAG interface (`usbfs`). Orphaned processes block all subsequent JTAG access with `LIBUSB_ERROR_IO`.

### OpenOCD Standard Procedures
Always use OpenOCD JTAG commands to control the target hardware:

1.  **Target Reset (Clean JTAG CPU Reset, No Strapping Pin Hazard):**
    *   **ESP32-P4 (JC4880P443C, USB `2-1.4.3`):**
        ```bash
        /home/ai/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32/bin/openocd -f board/esp32p4-builtin.cfg -c "adapter usb location 2-1.4.3" -c "init; reset run; shutdown"
        ```
    *   **ESP32-S3 (Lilka v2.3, USB `2-1.4.4`):**
        ```bash
        /home/ai/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32/bin/openocd -f board/esp32s3-builtin.cfg -c "adapter usb location 2-1.4.4" -c "init; reset run; shutdown"
        ```

2.  **Flash Firmware & Verify via JTAG:**
    *   **ESP32-P4:**
        ```bash
        /home/ai/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32/bin/openocd -f board/esp32p4-builtin.cfg -c "adapter usb location 2-1.4.3" -c "program_esp_bins build flasher_args.json verify reset exit"
        ```
    *   **ESP32-S3:**
        ```bash
        /home/ai/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32/bin/openocd -f board/esp32s3-builtin.cfg -c "adapter usb location 2-1.4.4" -c "program_esp_bins build flasher_args.json verify reset exit"
        ```

3.  **Live Inspection & Crash Diagnosis (GDB + OpenOCD):**
    *   Attach to running target without resetting:
        ```bash
        riscv32-esp-elf-gdb -batch -ex "target remote :3333" -ex "interrupt" -ex "info threads" -ex "thread apply all bt" -ex "detach" -ex "quit" build/eos.elf
        ```
    *   Check Program Counter and core state:
        ```bash
        openocd -f board/esp32p4-builtin.cfg -c "adapter usb location 2-1.4.3" -c "init; halt; targets; reg pc; resume; shutdown"
        ```

4.  **Automated Test Harness Execution (`tools/hardware_test.py`):**
    *   Tests interact with the running shell prompt using active canary pinging (`echo __EOS_READY__\n`) and non-blocking I/O.
    *   If target recovery is needed, trigger OpenOCD JTAG reset instead of DTR/RTS pin toggling.
