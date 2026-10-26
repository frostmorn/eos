# EOS Project: ESP Operating System

EOS is an advanced lightweight operating system and abstraction layer for the ESP32 (specifically targeting ESP32-S3). It implements a **Unified Device Model**, transforming heterogeneous hardware peripherals into a hierarchical **Device Tree** accessible via a standard **Virtual Filesystem (VFS)** interface under `/dev`.

---

## Core Design Goals

1. **Root Directory VFS Handling:** Unlike standard ESP-IDF limitations prohibiting direct root directory (`/`) mounting, EOS utilizes `eos_vfs_register()` and linker-based wrappers to intercept and manage virtual filesystem registrations cleanly.
2. **Compile-Time Registration:** Eliminates central maintenance of driver and application lists by utilizing custom linker scripts located in `main/eld` for automatic compile-time registration.
3. **"Everything is a File" Philosophy:** Standardizes hardware control through POSIX I/O (`open`, `close`, `read`, `write`, `lseek`) and unified, scope-specific `ioctl()` commands.
4. **Resource Leak Prevention:** Automatically tracks and cleans up system resources (file descriptors, directories, and thread contexts) per application to guarantee high system stability and survival.

---

## Supported Hardware Boards

EOS provides board configurations under `main/eboard/`:
1. **`lilka_v23` (ESP32-S3):** Handheld console target featuring SPI display, SD card storage, GPIO keyboard, and audio.
2. **`esp32s3_qemu`:** Emulator target running ESP32-S3 in QEMU with root flash storage and GPIO bus support.
3. **`jc4880p443c_i_w_y` (ESP32-P4):** Advanced target featuring MIPI-DSI display controllers, touch controllers, audio codecs, and SDMMC storage.

---

## Supported Peripherals & Drivers

*   **Bus Drivers (`edriver/bus/`):** Root bus, SPI, I2C, USB, and GPIO.
*   **Display Drivers (`edriver/display/`):** ST7789 (SPI TFT) and ST7701s (MIPI-DSI / RGB).
*   **Input Drivers (`edriver/input/`):** Keyboard/keypad active-low polling and GT911 capacitive touchscreens.
*   **Audio Drivers (`edriver/sound/`):** MAX98357 I2S audio codec.
*   **Storage & Block Drivers (`edriver/storage/`):** SD/SDIO cards, flash storage, and partition management (`diskpart.c`).
*   **Network Drivers & Subsystems (`edriver/net/`):** Wi-Fi STA mode, AP mode, network bridging, and TCP/IP stack initialization (`net_common.c`).
*   **Filesystem Subsystems (`ecore/`):** Root `tmpfs`, hierarchical `devfs`, executable `binfs`, process filesystem `procfs`, and pipe ring-buffers `pipefs`.
*   **Execution & Compatibility Layers:** Statically linked ELF binaries (`.eos_bins`), POSIX/FreeRTOS thread context wrappers with leak detection, and SDL 1.2 / 2.0 VFS compatibility layers.

---

## What Is NOT Supported Yet (Planned)

*   **SDIO & 1-Wire Bus Drivers:** Driver source stubs (`sdio.c`, `onewire.c`) currently exist as empty files and are planned for future releases.
*   **Bluetooth / BLE Stack Integration:** Full Bluetooth stack or BLE GATT profile abstractions are not yet integrated into the Unified Device Model.
*   **USB OTG Mass Storage Class:** USB host/device mass storage class driver emulation is planned but not fully implemented.
*   **MMU-based Process Isolation / fork():** Due to microcontroller architecture constraints, tasks run in isolated thread contexts with shared memory space rather than full virtual memory process separation (no POSIX `fork()`).
*   **Arbitrary Deep-Sleep State Retention:** Power-management deep sleep state retention for arbitrary peripheral driver states is currently limited.

---

## Topic Index

*   [Development Guide](./DEVELOPMENT.md) — Environment setup, target board configuration, build commands, and debugging workflows.
*   [Documentation Center](./docs/Readme.md) — Comprehensive architecture and driver scope guides.
