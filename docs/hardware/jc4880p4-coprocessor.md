# Guition JC4880P4 (ESP32-P4 + ESP32-C6) Co-Processor Architecture & Flashing Guide

This document describes the dual-chip architecture of the **Guition JC4880P4** (module `JC-ESP32P4-M3`, target `jc4880p443c_i_w_y`), the ESP-Hosted SDIO communication bus, and the step-by-step procedure for upgrading the **ESP32-C6** wireless co-processor firmware.

---

## 1. Dual-Chip System Topology

The Guition JC4880P4 features a split-architecture design:

* **Primary Application Processor (Host):**
  * **ESP32-P4** (Dual-Core RISC-V @ 400 MHz, 32 MB PSRAM, MIPI-DSI ST7701S display, GT911 touch, SDMMC storage).
  * The ESP32-P4 silicon does **not** feature an integrated 2.4 GHz radio.
* **Wireless Co-Processor (Slave):**
  * **ESP32-C6** providing Wi-Fi 6 (802.11ax) and Bluetooth 5 (LE).
* **Interconnect:**
  * **SDIO Slot 1 (4-bit @ 40 MHz):** CLK (GPIO 18), CMD (GPIO 19), D0–D3 (GPIOs 14, 15, 16, 17).
  * **Slave Reset:** GPIO 54 connected to `C6_CHIP_PU`.
  * **Protocol:** [ESP-Hosted](https://github.com/espressif/esp-hosted) transport using remote RPC over SDIO.

```text
+---------------------------------------------------------------------------------+
|                               Guition JC4880P4                                  |
|                                                                                 |
|  +------------------------+      SDIO Slot 1 (4-bit, 40 MHz)      +-----------+ |
|  |  ESP32-P4 Host         | <===================================> | ESP32-C6  | |
|  |  - EOS Operating System|                                       | Co-Proc   | |
|  |  - VFS, Display, Audio | ------[ GPIO 54: Reset ]------------> | (Wi-Fi/BT)| |
|  +------------------------+                                       +-----+-----+ |
|                                                                         |       |
|                                                     UART0, Boot, Reset  |       |
|                                                          (Header JP1)   v       |
|                                                                    [JP1 Header] |
+---------------------------------------------------------------------------------+
```

---

## 2. Why Co-Processor Firmware Flashing is Needed

1. **Factory Firmware Mismatch:**
   * Boards ship from the factory with ESP-Hosted slave firmware **`v2.3.0`** (built on ESP-IDF 5.1).
   * Modern ESP-IDF 6.0 and the latest ESP-Hosted host component run version **`v2.13.0`**.
   * During system boot, ESP-Hosted flags this incompatibility:
     ```text
     W transport: Version mismatch: Host [2.13.0] > Co-proc [2.3.0] ==> Upgrade co-proc to avoid RPC timeouts
     W transport: Co-processor fw did not report transport size, and may not be compatible with host.
     ```
2. **RPC Protocol & Feature Changes:**
   * Older `v2.3.0` firmware does not support modern protobuf extensions (such as per-band `channel_bitmap` and extended scan time configurations).
   * Mismatched RPCs cause connection drops (`ASSOC_LEAVE`), scan timeouts, or empty AP lists.
3. **Permanent One-Time Fix:**
   * Flashing the ESP32-C6 to a matching `2.12.x` or `2.13.x` firmware (`network_adapter_esp32c6.bin`) permanently stores the update in the C6's dedicated flash chip.
   * It survives all subsequent re-flashes and development iterations of the ESP32-P4 firmware.

---

## 3. Why Upgrading is Done Manually (Not Automated by P4)

1. **Flash Memory & Partition Constraints:**
   * The compiled C6 slave binary is **~1.5 MB**.
   * The P4's app partition on this board is **1.5 MB** (`0x177000`), with `eos.bin` already occupying ~1.39 MB. There is no space to embed a second full firmware image in the host binary.
2. **Hardware Pin Routing:**
   * The C6's UART programming lines (`C6_U0RXD`, `C6_U0TXD`) and bootloader select pin (`C6_IO9`) are **not routed to the ESP32-P4**.
   * They are exclusively routed to the external **JP1 Expand-IO header** on the back of the board.
3. **In-Band OTA Validation in v2.3.0:**
   * Attempting to send new firmware over SDIO using `esp_hosted_slave_ota_*` to the stock `v2.3.0` slave triggers `OTA_VALIDATE_FAILED` due to older bootloader signature checks. Direct UART flashing on JP1 bypasses this constraint cleanly.

---

## 4. JP1 (Expand-IO) Header Pinout & Physical Location

The **JP1 header** is a 26-pin (2×13, 2.54 mm pitch) dual-row header located on the rear of the board opposite the display:

```text
       +-------------------------------------------------------------+
       | [Display Side: Front]                                       |
       |                      GUITION JC4880P4 (REAR VIEW)           |
       |                                                             |
       |   +-----------------------+                                 |
       |   |                       |        [ JP1 2x13 HEADER ]      |
       |   |    JC-ESP32P4-M3      |       .-----------------.       |
       |   |      (SOM Module      |       | (1)  [.] [.] (2)|       |
       |   |       P4 + C6)        |       |      [.] [.]    |       |
       |   |                       |       |      [.] [.]    |       |
       |   |                       |       |      [.] [.]    |       |
       |   +-----------------------+       |      [.] [.]    |       |
       |                                   |      [.] [.]    |       |
       |   [TF / MicroSD Slot]             |      [.] [.]    |       |
       |                                   |      [.] [.]    |       |
       |                                   |      [.] [.]    |       |
       |                                   | (19) [.] [.] (20)<-- RXD|
       |   [USB-C 1]     [USB-C 2]         | (21) [.] [.] (22)<-- TXD|
       |   (High-Speed)  (Full-Speed/UART) | (23) [.] [.] (24)<-- IO9|
       |                                   | (25) [.] [.] (26)<-- PU |
       +-----------------------------------'-------------------------+
```

### Pin Mapping Matrix:

| Pin (Left / Odd) | Signal | Pin (Right / Even) | Signal | Description / Flashing Connection |
|:---|:---|:---|:---|:---|
| **Pin 1** | 3.3V | **Pin 2** | 5V | Power Rails |
| **Pin 3** | GND | **Pin 4** | GND | Ground reference |
| **Pin 5** | GPIO52 | **Pin 6** | GPIO33 | ESP32-P4 GPIOs |
| **Pin 7** | GPIO51 | **Pin 8** | GPIO31 | ESP32-P4 GPIOs |
| **Pin 9** | GPIO50 | **Pin 10** | GPIO30 | ESP32-P4 GPIOs |
| **Pin 11** | GPIO49 | **Pin 12** | GPIO29 | ESP32-P4 GPIOs |
| **Pin 13** | GPIO35 | **Pin 14** | NC | |
| **Pin 15** | GPIO34 | **Pin 16** | **GND** | **Connect to USB-UART Adapter GND** |
| **Pin 17** | GPIO32 | **Pin 18** | 3.3V | |
| **Pin 19** | GPIO28 | **Pin 20** | **C6_U0RXD** | **Connect to USB-UART Adapter TX** |
| **Pin 21** | I2C_SDA | **Pin 22** | **C6_U0TXD** | **Connect to USB-UART Adapter RX** |
| **Pin 23** | I2C_SCL | **Pin 24** | **C6_IO9** | **Connect to GND for Bootloader Mode** |
| **Pin 25** | NC | **Pin 26** | **C6_CHIP_PU** | Pulse to GND to reset C6 |

---

## 5. Step-by-Step Flashing Procedure

### Prerequisites
* 3.3V USB-to-UART adapter (e.g. CP2102, FT232, or CH340).
* 4 Dupont jumper wires.
* Python `esptool`: `pip install esptool`.

### Step 1: Wire the Connections
Connect the USB-UART adapter to header JP1:
1. **USB-UART GND** $\rightarrow$ **JP1 Pin 16** (GND)
2. **USB-UART TX** $\rightarrow$ **JP1 Pin 20** (C6_U0RXD)
3. **USB-UART RX** $\rightarrow$ **JP1 Pin 22** (C6_U0TXD)
4. Jumper wire: Connect **JP1 Pin 24** (C6_IO9) to **JP1 Pin 16** (GND) to force bootloader mode.

### Step 2: Put ESP32-C6 into Bootloader Mode
1. Power the board via USB-C.
2. Momentarily touch a wire from **JP1 Pin 26** (C6_CHIP_PU) to **GND** (1 second) and release.
3. The ESP32-C6 is now waiting in ROM bootloader mode.

### Step 3: Flash Modern Firmware
Run `esptool.py` to write the matching ESP-Hosted slave firmware:
```bash
esptool.py --chip esp32c6 -p /dev/ttyUSB0 -b 921600 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0 network_adapter_esp32c6.bin
```

### Step 4: Disconnect Bootloader Pin
1. Disconnect the jumper between **JP1 Pin 24** (IO9) and GND.
2. Momentarily pulse **JP1 Pin 26** (C6_CHIP_PU) to GND once more to boot the new firmware into normal operational mode.
3. Reset or reboot the ESP32-P4 host. The co-processor version warning will be gone and Wi-Fi 6 association will succeed cleanly.
