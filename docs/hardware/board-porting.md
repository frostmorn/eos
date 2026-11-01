# Board Porting & Declarative Target Architecture

EOS supports multiple target hardware boards located under `main/eboard/`. Hardware attachment and initialization in EOS is **declarative**: boards do not require custom C initialization files (`<board>.c` or `board.h`). Instead, each board specifies its hardware topology, pin assignments, and startup sequence through a shell script (`init.sh`) and an optimized ESP-IDF configuration (`sdkconfig`).

---

## 1. Board Directory Structure

Each board target directory under `main/eboard/<board_name>/` contains:
* **`init.sh`**: Declarative shell script that brings up buses and peripherals using `devctl attach`.
* **`sdkconfig`**: Tailored ESP-IDF configuration for the target SoC (e.g. ESP32-S3 or ESP32-P4), memory layout (PSRAM, flash frequency), and hardware peripheral clocks.

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
1. **Validates** the board directory in `main/eboard/<board_name>/`.
2. **Synchronizes `sdkconfig`**: Copies `main/eboard/<board_name>/sdkconfig` to the project root `sdkconfig`.
3. **Synchronizes `init.sh`**: Copies `main/eboard/<board_name>/init.sh` to the project root `init.sh`.
4. **Generates Preprocessor Definitions**: Writes target board identifiers (e.g., `#define EOS_TARGET_LILKA_V23 1`) to `main/pregen.h`.

---

## 3. Declarative Hardware Initialization (`init.sh` & `devctl`)

### Boot Sequence
During system startup in `main/eos.c`:
1. Core subsystems initialize: `tmpfs`, `log`, `pipefs`, `capsmgr`, `devtree`, `devfs`, `procfs`, and `binfs`.
2. `init.sh` is embedded into the firmware image as a text asset via CMake (`target_add_binary_data(eos.elf "init.sh" TEXT)`) and exposed under `/bin/init.sh` in `binfs`.
3. The main thread executes `system("init.sh")`.
4. `init.sh` runs sequentially line-by-line, calling `devctl attach` to construct the device tree dynamically. If `init.sh` completes or is aborted, EOS drops into the interactive micro-shell (`ush`).

### Declarative Attachment Syntax
Devices are attached dynamically via `devctl attach`:
```bash
devctl attach <scope> <driver> [parent] [--pin key=val,...] [--cfg key=val,...]
```
* **`<scope>`**: Subsystem driver scope (`bus`, `display`, `input`, `sound`, `storage`, `net`, `power`).
* **`<driver>`**: Registered driver name (e.g., `spi`, `i2c`, `sdio`, `st7789`, `st7701s`, `gt911`, `gpio_keyboard`, `flash`, `sd`, `wifi`, `sta`, `ap`, `max98357`, `battery`).
* **`[parent]`**: Parent device node name in the device tree (defaults to `root` if omitted).
* **`--pin key=val,...`**: Hardware pin assignments (e.g., `--pin sclk=18,mosi=17,miso=8`).
* **`--cfg key=val,...`**: Peripheral configuration parameters (e.g., `--cfg host=1,dma=3,clock_speed_hz=80000000`).

---

## 4. How to Add a New Board Port in 4 Steps

1. **Create Directory:** `mkdir -p main/eboard/<my_board>`
2. **Configure ESP-IDF:** Create `main/eboard/<my_board>/sdkconfig` with target chip, PSRAM mode, and clock settings.
3. **Draft Startup Script:** Create `main/eboard/<my_board>/init.sh` with `devctl attach` commands declaring your bus and peripheral topology.
4. **Activate & Build:**
   ```bash
   python3 tools/setboard.py <my_board>
   make build
   ```
