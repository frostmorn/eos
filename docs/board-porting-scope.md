# Board Porting & Declarative Hardware Target Architecture

EOS supports multiple target hardware boards located under `main/eboard/`. Hardware attachment and initialization in EOS is **declarative**: boards do not require custom C initialization files (`<board>.c` or `board.h`). Instead, each board specifies its hardware topology, pin assignments, and startup sequence through a shell script (`init.sh`) and an optimized ESP-IDF configuration (`sdkconfig`).

---

## 1. Board Directory Structure

Each board target directory under `main/eboard/<board_name>/` contains:
*   **`init.sh`**: Declarative shell script that brings up buses and peripherals using `devctl attach`.
*   **`sdkconfig`**: Tailored ESP-IDF configuration for the target SoC (e.g. ESP32-S3 or ESP32-P4), memory layout (PSRAM, flash frequency), and hardware peripheral clocks.

---

## 2. Board Selection Tool (`tools/setboard.py`)

Before compiling EOS, developers select the target board via the build system:
```bash
make setboard
```
or directly via:
```bash
python3 tools/setboard.py <board_name>
```

The script performs the following synchronization steps:
1.  **Validates** the board directory in `main/eboard/<board_name>/`.
2.  **Synchronizes `sdkconfig`**: Copies `main/eboard/<board_name>/sdkconfig` to the project root `sdkconfig`.
3.  **Synchronizes `init.sh`**: Copies `main/eboard/<board_name>/init.sh` to the project root `init.sh`.
4.  **Generates Preprocessor Definitions**: Writes target board identifiers (e.g., `#define EOS_TARGET_JC4880P443C_I_W_Y 1`) to `main/pregen.h`.

---

## 3. Declarative Hardware Initialization (`init.sh` & `devctl`)

### Boot Sequence
During system startup in `main/eos.c`:
1.  Core subsystems initialize: `tmpfs`, `log`, `pipefs`, `capsmgr`, `devtree`, `devfs`, `procfs`, and `binfs`.
2.  `init.sh` is embedded into the firmware image as a text asset via CMake (`target_add_binary_data(eos.elf "init.sh" TEXT)`) and exposed under `/bin/init.sh` in `binfs`.
3.  The main thread executes `system("init.sh")`.
4.  `init.sh` runs sequentially line-by-line, calling `devctl attach` to construct the device tree dynamically. If `init.sh` completes or is aborted, EOS drops into the interactive micro-shell (`ush`).

### Declarative Attachment Syntax
Devices are attached dynamically via `devctl attach`:
```bash
devctl attach <scope> <driver> [parent] [--pin key=val,...] [--cfg key=val,...]
```
*   **`<scope>`**: Subsystem driver scope (`bus`, `display`, `input`, `sound`, `storage`, `net`).
*   **`<driver>`**: Registered driver name (e.g., `spi`, `i2c`, `sdio`, `st7789`, `st7701s`, `gt911`, `keyboard`, `flash`, `sd`, `wifi`, `sta`, `ap`).
*   **`[parent]`**: Parent device node name in the device tree (defaults to `root` if omitted).
*   **`--pin key=val,...`**: Hardware pin assignments (e.g., `--pin sclk=18,mosi=17,miso=8` or `--pin cs=7,dc=15,en=46`).
*   **`--cfg key=val,...`**: Peripheral configuration parameters (e.g., `--cfg host=1,dma=3,clock_speed_hz=80000000`).

---

## 4. Supported Boards

*   **`lilka_v23` (ESP32-S3):**
    *   Target handheld console for Lilka v2.
    *   Peripherals: SPI host 1, ST7789 display controller (`/dev/fb0`), SD card storage (`/dev/sd0`), I2C bus 0, GPIO keyboard (`/dev/keyboard0`), and onboard Wi-Fi radio (`wifi0`, `sta0`, `ap0`).
*   **`esp32s3_qemu`:**
    *   Emulator target running ESP32-S3 in Espressif QEMU.
    *   Peripherals: Root flash storage (`/dev/flash0`), GPIO bus, and virtual QEMU RGB display controller (`/dev/fb0`, `esp_lcd_qemu_rgb`).
*   **`jc4880p443c_i_w_y` (ESP32-P4):**
    *   Dual-core RISC-V @ 400 MHz with 32 MB PSRAM.
    *   Peripherals: MIPI-DSI ST7701S display controller (`/dev/fb0`), GT911 capacitive touch (`/dev/input0` over I2C), GPIO bus, SDMMC slot 0 for SD card storage (`/dev/sd0`), and SDMMC slot 1 (lazy mode) hosting the external ESP32-C6 Wi-Fi co-processor via ESP-Hosted SDIO (see [JC4880P4 Co-Processor Guide](./jc4880p4-coprocessor-guide.md)).

---

## 5. Adding a New Board Port

1.  **Create Board Directory:** Create `main/eboard/<board_name>/`.
2.  **Configure ESP-IDF:** Create a tailored `sdkconfig` file optimized for the chip family, PSRAM speed, and clock sources.
3.  **Write `init.sh`:** Declare peripheral attachment commands using `devctl attach`.
4.  **Register Target:** Run `python3 tools/setboard.py <board_name>` to synchronize root files and generate preprocessor definitions.
5.  **Driver Scaffolding:** If new peripheral drivers are needed, run `make driver` (`tools/codegen.py`) to generate driver templates under `main/edriver/<scope>/`.
6.  **Verify Hardware:** Run automated hardware validation tests using `tools/hardware_test.py`.

---
[Back to Documentation Index](./Readme.md) | [See Bus Drivers](./bus-driver-scope.md) | [See BinFS Architecture](./binfs-and-binaries-scope.md)
