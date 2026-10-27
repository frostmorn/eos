# Guition JC4880P4 (ESP32-P4 + ESP32-C6) Co-Processor Architecture & Flashing Guide

This document describes the dual-chip architecture of the **Guition JC4880P4** (module `JC-ESP32P4-M3`, target `jc4880p443c_i_w_y`), the ESP-Hosted SDIO communication bus, and the step-by-step procedure for upgrading the **ESP32-C6** wireless co-processor firmware.

---

## 1. Dual-Chip System Topology

The Guition JC4880P4 features a split-architecture design:

*   **Primary Application Processor (Host):**
    *   **ESP32-P4** (Dual-Core RISC-V @ 400 MHz, 32 MB PSRAM, MIPI-DSI ST7701S display, GT911 touch, SDMMC storage).
    *   The ESP32-P4 silicon does **not** feature an integrated 2.4 GHz radio.
*   **Wireless Co-Processor (Slave):**
    *   **ESP32-C6** providing Wi-Fi 6 (802.11ax) and Bluetooth 5 (LE).
*   **Interconnect:**
    *   **SDIO Slot 1 (4-bit @ 40 MHz):** CLK (GPIO 18), CMD (GPIO 19), D0–D3 (GPIOs 14, 15, 16, 17).
    *   **Slave Reset:** GPIO 54 connected to `C6_CHIP_PU`.
    *   **Protocol:** [ESP-Hosted](https://github.com/espressif/esp-hosted) transport using remote RPC over SDIO.

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

1.  **Factory Firmware Mismatch:**
    *   Boards ship from the factory with ESP-Hosted slave firmware **`v2.3.0`** (built on ESP-IDF 5.1).
    *   Modern ESP-IDF 6.0 and the latest ESP-Hosted host component run version **`v2.13.0`**.
    *   During system boot, ESP-Hosted flags this incompatibility:
        ```text
        W transport: Version mismatch: Host [2.13.0] > Co-proc [2.3.0] ==> Upgrade co-proc to avoid RPC timeouts
        W transport: Co-processor fw did not report transport size, and may not be compatible with host.
        ```
2.  **RPC Protocol & Feature Changes:**
    *   Older `v2.3.0` firmware does not support modern protobuf extensions (such as per-band `channel_bitmap` and extended scan time configurations).
    *   Mismatched RPCs cause connection drops (`ASSOC_LEAVE`), scan timeouts, or empty AP lists.
3.  **Permanent One-Time Fix:**
    *   Flashing the ESP32-C6 to a matching `2.12.x` or `2.13.x` firmware (`network_adapter_esp32c6.bin`) permanently stores the update in the C6's own dedicated flash chip.
    *   It survives all subsequent re-flashes and development iterations of the ESP32-P4 firmware.

---

## 3. Why Upgrading is Done Manually (Not Automated by P4)

1.  **Flash Memory & Partition Constraints:**
    *   The compiled C6 slave binary is **~1.5 MB**.
    *   The P4's app partition on this board is **1.5 MB** (`0x177000`), with `eos.bin` already occupying ~1.39 MB. There is no space to embed a second full firmware image in the host binary.
2.  **Hardware Pin Routing:**
    *   The C6's UART programming lines (`C6_U0RXD`, `C6_U0TXD`) and bootloader select pin (`C6_IO9`) are **not routed to the ESP32-P4**.
    *   They are exclusively routed to the external **JP1 Expand-IO header** on the back of the board.
3.  **In-Band OTA Validation in v2.3.0:**
    *   Attempting to send new firmware over SDIO using `esp_hosted_slave_ota_*` to the stock `v2.3.0` slave triggers `OTA_VALIDATE_FAILED` due to older bootloader signature checks. Direct UART flashing on JP1 bypasses this constraint cleanly.

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
       |   +-----------------------+       |      [.] [.]    |       |
       |                                   |      [.] [.]    |       |
       |   [TF / MicroSD Slot]             |      [.] [.]    |       |
       |                                   |      [.] [.]    |       |
       |                                   |      [.] [.]    |       |
       |                                   |      [.] [.]    |       |
       |                                   | (19) [.] [.] (20)<-- RXD|
       |   [USB-C 1]     [USB-C 2]         | (21) [.] [.] (22)<-- TXD|
       |   (High-Speed)  (Full-Speed/UART) | (23) [.] [.] (24)<-- IO9|
       |                                   | (25) [.] [.] (26)<-- PU |
       +-----------------------------------'-------------------------+
```

### Pin Mapping Matrix

| Pin (Left / Odd) | Signal | Pin (Right / Even) | Signal | Description / Flashing Connection |
|:---|:---|:---|:---|:---|
| **Pin 1** | 3.3V | **Pin 2** | 5V | |
| **Pin 3** | GND | **Pin 4** | GND | Ground reference |
| **Pin 5** | GPIO52 | **Pin 6** | GPIO33 | |
| **Pin 7** | GPIO51 | **Pin 8** | GPIO31 | |
| **Pin 9** | GPIO50 | **Pin 10** | GPIO30 | |
| **Pin 11** | GPIO49 | **Pin 12** | GPIO29 | |
| **Pin 13** | GPIO35 | **Pin 14** | NC | |
| **Pin 15** | GPIO34 | **Pin 16** | **GND** | **Connect to USB-UART GND** |
| **Pin 17** | GPIO32 | **Pin 18** | 3.3V | |
| **Pin 19** | GPIO28 | **Pin 20** | **C6_U0RXD** | **Connect to USB-UART TX** |
| **Pin 21** | I2C_SDA | **Pin 22** | **C6_U0TXD** | **Connect to USB-UART RX** |
| **Pin 23** | I2C_SCL | **Pin 24** | **C6_IO9** | **Connect to GND for Bootloader Mode** |
| **Pin 25** | NC | **Pin 26** | **C6_CHIP_PU** | Pulse to GND to reset C6 |

---

## 5. Step-by-Step Flashing Procedure

### Firmware Binary Specification
The ESP32-C6 co-processor requires an ESP-Hosted slave firmware matching the host RPC version. The successfully verified and flashed image is:

* **File Name:** `network_adapter_esp32c6.bin`
* **Release Version:** **ESP-Hosted `v2.12.9`**
* **Upstream Source:** [ESPHome ESP-Hosted Prebuilt Mirror](https://esphome.github.io/esp-hosted-firmware/v2.12.9/network_adapter_esp32c6.bin)
* **File Size:** 1,209,600 bytes
* **ELF SHA-256:** `9267a0fae64e7fd6c4676272343ba17dc100e5b01e7dc5e8f5b2161d56a91ed2`
* **Compiled On:** ESP-IDF `v5.5.4` (Jun 9 2026, 09:58:57)
* **Target Partition:** `ota_0` at offset `0x10000` (1,536 KB allocation)
* **Otadata Location:** `0xd000` (8 KB erased to default to `ota_0`)
* **Host Compatibility:** Fully compatible with ESP-IDF 6.0 / 6.1 `esp_hosted` host component (`v2.13.0`), eliminating the factory `v2.3.0` RPC timeout loops and dropped packets (`ASSOC_LEAVE`).

### Prerequisites
1.  **3.3V USB-to-UART Adapter** (CP2102, FT232, CH340, etc.).
2.  `esptool` installed (`python3 -m pip install esptool`).
3.  Matching ESP32-C6 ESP-Hosted firmware:
    ```bash
    curl -LO https://esphome.github.io/esp-hosted-firmware/v2.12.9/network_adapter_esp32c6.bin
    ```

### Wiring Diagram
```text
 USB-to-UART Adapter (3.3V)                     JP1 Header (Even Pins)
+--------------------------+                   +-----------------------+
|  TX                      | ----------------> | Pin 20: C6_U0RXD      |
|  RX                      | <---------------- | Pin 22: C6_U0TXD      |
|  GND                     | ----------------> | Pin 16: GND           |
+--------------------------+                   +-----------------------+

 Jumper (Bootloader Download Mode):
 Connect Pin 24 (C6_IO9) to Pin 16 (GND) with a temporary jumper wire.
 (Leave adapter 3.3V power wire DISCONNECTED; the board supplies power via USB).
```

### Execution Steps

1.  **Park the ESP32-P4 in Bootloader Mode:**
    > [!IMPORTANT]
    > If the ESP32-P4 is running the EOS application, its Wi-Fi supervision task will toggle GPIO 54 (`C6_CHIP_PU`) to reset the C6, causing the flash process to abort mid-write. Halt the P4 first:
    ```bash
    python3 -m esptool --chip esp32p4 -p /dev/ttyACM0 --before default_reset --after no_reset flash_id
    ```

2.  **Flash the ESP32-C6:**
    With Pin 24 (`C6_IO9`) grounded, write the new image to the C6:
    ```bash
    python3 -m esptool --chip esp32c6 -p /dev/ttyUSB0 -b 115200 write_flash 0x10000 network_adapter_esp32c6.bin
    ```

3.  **Clear the otadata Partition:**
    Ensure the bootloader selects the freshly written `ota_0` app partition:
    ```bash
    python3 -m esptool --chip esp32c6 -p /dev/ttyUSB0 erase_region 0xd000 0x2000
    ```

4.  **Reset Both Chips:**
    Remove the jumper between Pin 24 (`C6_IO9`) and GND, then reset both chips:
    ```bash
    python3 -m esptool --chip esp32c6 -p /dev/ttyUSB0 --after hard_reset flash_id
    python3 -m esptool --chip esp32p4 -p /dev/ttyACM0 --after hard_reset flash_id
    ```

---

## 6. Verification in EOS

Once rebooted, connect to the serial console on `/dev/ttyACM0` (or `/dev/ttyACM1`):
1.  **Verify ESP-Hosted Boot Version:**
    The system boot log confirms matching protocol versions and C6 boot-up:
    ```text
    I (3558) transport: Identified slave [esp32c6]
    I (3558) transport: SDIO mode: slave: streaming, host: streaming
    I (3559) transport: capabilities: 0xd
    I (3559) transport: Features supported are:
    I (3559) transport: 	 * WLAN
    I (3559) transport: 	   - HCI over SDIO
    I (3559) transport: 	   - BLE only
    W (3560) transport: Version mismatch: Host [2.13.0] > Co-proc [2.12.0] ==> Upgrade co-proc to avoid RPC timeouts
    I (4435) RPC_WRAP: Coprocessor Boot-up
    ```
2.  **Verify Interface MAC Sync:**
    ```text
    ~/==(^_^) / > netctl
    1: wifi0: <UP,LOWER_UP,BROADCAST,MULTICAST> mtu 1500 qdisc noqueue state UP
        link/ieee802.11 00:00:00:00:00:00 brd ff:ff:ff:ff:ff:ff
        /* ext C6 (PU GPIO 54), ch 1, 20 dBm */
    2: sta0: <BROADCAST,MULTICAST> mtu 1500 qdisc noqueue state DOWN
        link/ether 12:34:56:78:9a:bc brd ff:ff:ff:ff:ff:ff
    ```
3.  **Verify Scanning via `wlanctl`:**
    ```text
    ~/==(^_^) / > wlanctl radio on
    wifi0: radio turned ON
    ~/==(^_^) / > wlanctl scan
    Scanning for WiFi networks (active mode0)...
    Found 1 access point(s):
    SSID                              BSSID              RSSI  CH  AUTH        
    -----------------------------------------------------------------------
    Example-Network                   12:34:56:78:9a:bc   -43   1  WPA2_PSK    

    SCAN_DONE:count=1
    ```

---

## 7. RF Antenna Hardware Architecture (0-Ohm Resistor vs IPEX)

The Guition JC4880P4 features both an onboard PCB trace antenna and an unpopulated/populated **IPEX / u.FL** micro-RF connector socket for external antennas.

```text
                     [0-ohm Resistor (Factory Default)]
                     +---[ 0R ]---> Onboard PCB Trace Antenna
ESP32-C6 RF Out -----+
                     +---( OPEN )--> IPEX / u.FL Connector (Disconnected by default)
```

> [!IMPORTANT]
> **Antenna selection is NOT automatic.** Connecting an external antenna to the IPEX socket while the 0-ohm jumper remains in its factory position will result in no signal to the external antenna.
> * **Factory Configuration:** The 0-ohm jumper resistor (`0402` or `0201` footprint) bridges the C6 RF line to the onboard PCB trace antenna.
> * **Switching to IPEX (External Antenna):** To route RF power to an external whip/pigtail antenna, the 0-ohm resistor must be desoldered and relocated (or target pads bridged with solder) to connect the C6 RF line to the IPEX center pin instead of the PCB trace.
> * **Onboard Antenna Range:** The onboard trace antenna provides reliable reception for nearby APs and phone hotspots (e.g. RSSI > -50 dBm). Resoldering to IPEX is only necessary for long-range installations or metal/shielded enclosures.

---
[Back to Documentation Index](./Readme.md) | [Network Driver Scope](./network-driver-scope.md) | [Board Porting Scope](./board-porting-scope.md)
