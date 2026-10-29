# EOS Project: ESP Operating System

EOS is an advanced lightweight operating system and abstraction layer for ESP32 microcontrollers (specifically targeting ESP32-S3 and ESP32-P4). It implements a **Unified Device Model**, transforming heterogeneous hardware peripherals into a hierarchical **Device Tree** accessible via a standard **Virtual Filesystem (VFS)** interface under `/dev`.

---

## Core Design Goals

1. **Root Directory VFS Handling:** Unlike standard ESP-IDF limitations prohibiting direct root directory (`/`) mounting, EOS utilizes `eos_vfs_register()` and linker-based wrappers to intercept and manage virtual filesystem registrations cleanly.
2. **Compile-Time Registration:** Eliminates central maintenance of driver and application lists by utilizing custom linker scripts located in `main/eld` for automatic compile-time registration.
3. **Declarative Hardware Architecture:** Hardware attachments are fully declarative via `/bin/init.sh` and `devctl attach`, eliminating hardcoded C board files and enabling dynamic runtime peripheral topology configuration.
4. **"Everything is a File" Philosophy:** Standardizes hardware control through POSIX I/O (`open`, `close`, `read`, `write`, `lseek`) and unified, scope-specific `ioctl()` commands.
5. **Resource Leak Prevention:** Automatically tracks and cleans up system resources (file descriptors, directories, and thread contexts) per application to guarantee high system stability and survival.

---

## Supported Hardware Boards

EOS provides declarative board configurations under `main/eboard/`:
1. **`lilka_v23` (ESP32-S3):** Handheld console target featuring SPI display (ST7789), SD card storage, I2C bus, GPIO keyboard, and onboard Wi-Fi.
2. **`esp32s3_qemu`:** Emulator target running ESP32-S3 in Espressif QEMU with root flash storage, GPIO bus, and virtual QEMU RGB display (`esp_lcd_qemu_rgb`).
3. **`jc4880p443c_i_w_y` (ESP32-P4):** High-performance dual-core RISC-V @ 400 MHz target featuring MIPI-DSI display (ST7701S), capacitive touch (GT911), SDMMC slot 0 storage, and SDMMC slot 1 SDIO link to an ESP32-C6 Wi-Fi 6 / Bluetooth co-processor.

---

## Supported Peripherals & Drivers

*   **Bus Drivers (`edriver/bus/`):** Root bus, SPI, I2C, SDIO (SDMMC host, slot 0 and slot 1), USB, and GPIO.
*   **Display Drivers (`edriver/display/`):** ST7789 (SPI TFT), ST7701s (MIPI-DSI / RGB), and QEMU RGB virtual display.
*   **Input Drivers (`edriver/input/`):** Keyboard/keypad active-low polling and GT911 capacitive touchscreens.
*   **Audio Drivers (`edriver/sound/`):** MAX98357 I2S audio codec.
*   **Storage & Block Drivers (`edriver/storage/`):** SD/SDIO cards, flash storage, and partition management (`diskpart.c`).
*   **Network Drivers & Subsystems (`edriver/net/`):** Wi-Fi physical radio (`net/wifi`), STA mode (`net/sta`), AP mode (`net/ap`), and network bridging (`net/bridge`) with parent-child ioctl communication.
*   **Filesystem Subsystems (`ecore/`):** Root `tmpfs`, hierarchical `devfs`, executable `binfs` (supporting ELF binaries and embedded scripts), process filesystem `procfs`, and pipe ring-buffers `pipefs`.
*   **Execution & Compatibility Layers:** Statically linked ELF binaries (`.eos_bins`), POSIX/FreeRTOS thread context wrappers with leak detection, and SDL 1.2 / 2.0 VFS compatibility layers.

---

## What Is NOT Supported Yet (Planned)

*   **1-Wire Bus Driver:** Driver source stub (`onewire.c`) is planned for future releases.
*   **Bluetooth / BLE Stack Integration:** Full Bluetooth stack or BLE GATT profile abstractions are not yet integrated into the Unified Device Model.
*   **USB OTG Mass Storage Class:** USB host/device mass storage class driver emulation is planned but not fully implemented.
*   **MMU-based Process Isolation / fork():** Due to microcontroller architecture constraints, tasks run in isolated thread contexts with shared memory space rather than full virtual memory process separation (no POSIX `fork()`).
*   **Arbitrary Deep-Sleep State Retention:** Power-management deep sleep state retention for arbitrary peripheral driver states is currently limited.

---

## Topic Index

*   [Development Guide](./DEVELOPMENT.md) — Environment setup, target board configuration, build commands, and debugging workflows.
*   [Documentation Center](./docs/Readme.md) — Comprehensive architecture and driver scope guides.
