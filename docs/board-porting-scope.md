# Board Porting & Hardware Target Architecture

EOS supports multiple target hardware boards located under `main/eboard/`. Each target board defines its own hardware topology, pin assignments, peripheral attachment order, and SDK configuration.

---

## 1. Board Selection Tool (`tools/setboard.py`)

Before compiling EOS, developers select the target board via the build system:
```bash
make setboard
```
This script (`tools/setboard.py`) interacts with available board directories, copies the appropriate `sdkconfig` to the project root, and generates target preprocessor definitions in `main/pregen.h`.

---

## 2. Board Initialization (`eos_board_init()`)

Every board defines a `void eos_board_init()` function called during early OS boot (`main/eos.c`). This function is responsible for:
1.  **Claiming Reserved Pins:** Calling `eos_cap_claim()` on strapping pins, debug pins, and hardware-reserved lines to prevent accidental driver conflicts.
2.  **Allocating & Configuring Devices:** Allocating device slots (`eos_dev_alloc()`), assigning drivers found via `eos_drv_find()`, configuring pinmaps and cfg parameters, and attaching devices into the device tree via `eos_dev_attach()`.

---

## 3. Supported Boards

*   **`lilka_v23` (ESP32-S3):** Target board for Lilka V2 handheld consoles. Configures SPI bus 2, ST7789 display controller (`/dev/fb0`), SD card storage (`/dev/sd`), and GPIO input keyboard (`/dev/keyboard0`).
*   **`esp32s3_qemu`:** Emulator target running ESP32-S3 in QEMU. Initializes root flash storage and GPIO bus.
*   **`jc4880p443c_i_w_y` (ESP32-P4):** Advanced target board configuration featuring MIPI-DSI display controllers, touch controllers, audio codecs, and SDMMC storage.

---

## 4. Adding a New Board Port

1.  **Directory Structure:** Create a new folder under `main/eboard/<board_name>/`.
2.  **Configuration:** Provide a tailored `sdkconfig` file optimized for the target chip and peripherals.
3.  **Initialization Routine:** Implement `void eos_board_init()` in `<board_name>.c`, guarded by `#ifdef EOS_TARGET_<BOARD_NAME>`.
4.  **Register Target:** Update `tools/setboard.py` and CMake rules to recognize the new board option.

---
[Back to Documentation Index](./Readme.md)
