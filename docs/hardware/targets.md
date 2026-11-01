# EOS Hardware Target Profiles & Pinouts

EOS supports three primary hardware targets under `main/eboard/`:
1. **`lilka_v23`**: Physical handheld gaming & development console (ESP32-S3).
2. **`jc4880p443c_i_w_y`**: High-performance dual-core RISC-V smart display board (ESP32-P4 + ESP32-C6).
3. **`esp32s3_qemu`**: Headless & graphics virtual emulator target running under Espressif QEMU.

---

## 1. Lilka v2.3 Handheld Console (`lilka_v23`)

```text
+-----------------------------------------------------------------------------------+
|                                Lilka v2.3 Handheld                                |
|                                                                                   |
|      +---------------------------------------------------------------------+      |
|      |                  ST7789 TFT LCD (240x240 / 280x240)                 |      |
|      +---------------------------------------------------------------------+      |
|                                                                                   |
|   [D-PAD]                      [ACTION KEYS]               [AUDIO & POWER]        |
|   UP:     GPIO 14              A:      GPIO 4              MAX98357A: GPIO 42,1,2 |
|   DOWN:   GPIO 13              B:      GPIO 5              BATTERY:   GPIO 3      |
|   LEFT:   GPIO 12              C:      GPIO 6              I2C SDA:   GPIO 10     |
|   RIGHT:  GPIO 11              D:      GPIO 40             I2C SCL:   GPIO 11     |
|   SELECT: GPIO 9               START:  GPIO 10             ESC:       GPIO 0      |
+-----------------------------------------------------------------------------------+
```

### Hardware Specifications:
* **Microcontroller:** ESP32-S3 (Xtensa Dual-Core 32-bit LX7 @ 240 MHz).
* **Memory:** 512 KB Internal SRAM, 8 MB Octal SPI PSRAM, 16 MB Flash.
* **Connectivity:** Integrated 2.4 GHz Wi-Fi 802.11 b/g/n & Bluetooth 5 (LE).

### Peripheral Pinout Mapping:

| Peripheral / Signal | ESP32-S3 Pin | Driver Scope & Name | Device Node | Notes |
| :--- | :---: | :--- | :--- | :--- |
| **SPI SCLK** | `GPIO 18` | `bus/spi` | `/dev/spi0` | SPI Host 1 clock line |
| **SPI MOSI** | `GPIO 17` | `bus/spi` | `/dev/spi0` | SPI Host 1 data out |
| **SPI MISO** | `GPIO 8` | `bus/spi` | `/dev/spi0` | SPI Host 1 data in |
| **Display CS** | `GPIO 7` | `display/st7789` | `/dev/fb0` | Display chip select |
| **Display DC** | `GPIO 15` | `display/st7789` | `/dev/fb0` | Data/command select |
| **Display RST** | `GPIO 45` | `display/st7789` | `/dev/fb0` | Panel hardware reset |
| **Display Backlight**| `GPIO 46` | `display/st7789` | `/dev/fb0` | PWM / GPIO backlight |
| **MicroSD CS** | `GPIO 16` | `storage/sd` | `/dev/sd0` | SD card SPI chip select |
| **I2C SDA** | `GPIO 10` | `bus/i2c` | `/dev/i2c0` | Shared with Start key |
| **I2C SCL** | `GPIO 11` | `bus/i2c` | `/dev/i2c0` | Shared with Right key |
| **I2S BCLK** | `GPIO 42` | `sound/max98357` | `/dev/sound0` | Bit clock |
| **I2S WS (LRCK)** | `GPIO 1` | `sound/max98357` | `/dev/sound0` | Word select |
| **I2S DOUT** | `GPIO 2` | `sound/max98357` | `/dev/sound0` | Audio PCM data |
| **Battery ADC** | `GPIO 3` | `bus/adc`, `power/battery` | `/dev/battery0` | Resistor divider: R_top=33k, R_bot=100k |

---

## 2. Guition JC4880P4 Smart Display (`jc4880p443c_i_w_y`)

```text
+-----------------------------------------------------------------------------------+
|                        Guition JC4880P4 (JC-ESP32P4-M3)                           |
|                                                                                   |
|  +--------------------------------+       +------------------------------------+  |
|  |  ESP32-P4 Application Host     |       |  ESP32-C6 Wireless Co-Processor    |  |
|  |  Dual RISC-V @ 400 MHz         |       |  Wi-Fi 6 (802.11ax) + BLE 5        |  |
|  |  32 MB PSRAM                   | <===> |  Firmware v2.13.0 (ESP-Hosted)     |  |
|  |  MIPI-DSI ST7701S (480x480)    | SDIO1 |  Reset: GPIO 54                    |  |
|  |  GT911 Capacitive Touch        |       |  Programming: Header JP1           |  |
|  +--------------------------------+       +------------------------------------+  |
+-----------------------------------------------------------------------------------+
```

### Hardware Specifications:
* **Host Processor:** ESP32-P4 (Dual-Core 32-bit RISC-V @ 400 MHz).
* **Memory:** 768 KB Internal SRAM, 32 MB High-Speed PSRAM, 16 MB Flash.
* **Co-Processor:** ESP32-C6 providing Wi-Fi 6 and BLE over high-speed SDIO.

### Peripheral Pinout Mapping:

| Peripheral / Signal | Pin Connection | Driver Scope & Name | Device Node | Notes |
| :--- | :---: | :--- | :--- | :--- |
| **MIPI-DSI Reset** | `GPIO 27` | `display/st7701s` | `/dev/fb0` | 480x480 MIPI-DSI panel |
| **Display Backlight**| `GPIO 26` | `display/st7701s` | `/dev/fb0` | Active high enable |
| **Touch I2C SDA** | `GPIO 7` | `bus/i2c` | `/dev/i2c0` | GT911 touch data |
| **Touch I2C SCL** | `GPIO 8` | `bus/i2c` | `/dev/i2c0` | GT911 touch clock |
| **Touch RST** | `GPIO 23` | `input/gt911` | `/dev/input0` | Hardware reset |
| **Touch IRQ** | `GPIO 22` | `input/gt911` | `/dev/input0` | Interrupt line |
| **SDMMC Slot 0 CLK** | `GPIO 43` | `storage/sd` | `/dev/sd0` | SD Card Slot 0 (4-bit @ 40 MHz) |
| **SDMMC Slot 0 CMD** | `GPIO 44` | `storage/sd` | `/dev/sd0` | Command line |
| **SDMMC Slot 0 D0–D3**| `GPIO 39–42` | `storage/sd` | `/dev/sd0` | Data lines |
| **SDMMC Slot 1 CLK** | `GPIO 18` | `net/wifi` | `/dev/wifi0` | SDIO Slot 1 -> ESP32-C6 |
| **SDMMC Slot 1 CMD** | `GPIO 19` | `net/wifi` | `/dev/wifi0` | Command line |
| **SDMMC Slot 1 D0–D3**| `GPIO 14–17` | `net/wifi` | `/dev/wifi0` | Data lines |
| **C6 Chip Power-Up** | `GPIO 54` | `net/wifi` | `/dev/wifi0` | Co-processor reset control |

---

## 3. Espressif QEMU Emulator (`esp32s3_qemu`)

### Specifications:
* **Architecture:** ESP32-S3 dual-core emulation inside Espressif QEMU.
* **Display:** Virtual RGB Display Controller (`display/qemu_rgb`, 320x240 RGB565 via `esp_lcd_qemu_rgb`).
* **Input:** Virtual Keyboard FIFO (`input/gpio_keyboard`) supporting synthetic key event injection.
* **Storage:** Root flash partition storage (`storage/flash`).
* **Usage:** Continuous integration, headless automated testing (`tools/hardware_test.py`), graphics development.
