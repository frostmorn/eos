# ADC Bus Driver & Battery Power Driver Contracts

This document details the driver contracts, hardware integration, calibration mechanisms, and IOCTL interfaces for the **ADC bus driver** and the **battery power driver** in EOS.

---

## 1. ADC Bus Driver (`edriver/bus/adc/`)

* **Driver Scope:** `bus`
* **Driver Name:** `adc`
* **Device Node:** `/dev/adc0`
* **Parent:** `root`
* **Attachment Example:**
  ```sh
  devctl attach bus adc root
  ```

### Overview
The `bus/adc` driver provides analog-to-digital conversion using the ESP32-S3 `esp_adc/adc_oneshot` API combined with factory eFuse curve-fitting calibration (`esp_adc/adc_cali`). It manages ADC units 1 and 2 on demand and converts 12-bit raw readings directly into calibrated millivolts.

### VFS Interface
* **`read()`**: Reading `/dev/adc0` outputs the active default pin, raw sample, and calibrated millivolts:
  ```
  pin: 3
  raw: 4037
  mv: 3098
  ```
  *(Returns EOF after reading so `cat /dev/adc0` outputs once and terminates).*

### ADC IOCTL Interface

| IOCTL Command | Code | Argument Type | Description |
| :--- | :---: | :--- | :--- |
| `EOS_ADC_IOCTL_READ_RAW` | `600` | `eos_adc_reading_t *r` | In: `r->pin`. Out: sets `r->raw` (0–4095). |
| `EOS_ADC_IOCTL_READ_VOLTAGE` | `601` | `eos_adc_reading_t *r` | In: `r->pin`. Out: sets `r->raw` and factory-calibrated `r->voltage_mv`. |

```c
typedef struct {
  uint32_t pin;         // GPIO pin number (e.g. 3)
  int32_t  raw;         // Raw 12-bit conversion value (0 - 4095)
  int32_t  voltage_mv;  // Calibrated voltage in millivolts
} eos_adc_reading_t;
```

---

## 2. Battery Power Driver (`edriver/power/battery/`)

* **Driver Scope:** `power`
* **Driver Name:** `battery`
* **Device Node:** `/dev/battery0`
* **Parent:** `adc0`
* **Attachment Example:**
  ```sh
  devctl attach power battery adc0 --pin adc=3 --cfg r_top=33,r_bottom=100,v_empty=3200,v_full=4200
  ```

### Lilka v2.3 Hardware Circuit
The Lilka handheld console routes the single-cell LiPo battery through a resistor divider to ESP32-S3 **GPIO 3** (`ADC1_CHANNEL_2`):

```text
    VBAT (LiPo 3.2V - 4.2V)
        │
       ┌┴┐
       │ │ R4 = 33 kΩ (top)
       └┬┘
        ├─── VVAL (GPIO 3)
       ┌┴┐
       │ │ R2 = 100 kΩ (bottom)
       └┬┘
        │
       GND
```

* **Divider Ratio:** $V_{\text{pin}} = V_{\text{bat}} \times \frac{100}{133} \approx 0.75188 \times V_{\text{bat}}$
* **Inverse Battery Scaling:** $V_{\text{bat}} = V_{\text{pin}} \times \frac{133}{100} = 1.33 \times V_{\text{pin}}$

### State of Charge (SoC) Engine
1. **Multi-Stage Filtering (Noise & Load Sag Mitigation):**
   * Collects a burst of 16 samples and computes the **median** to eliminate switching regulator spikes.
   * Feeds the median into an **Exponential Moving Average (EMA)** filter ($\alpha = 0.2$) to smooth out momentary voltage drops caused by audio amplifier bursts (MAX98357A) or Wi-Fi packets.
2. **Empirical 12-Point LiPo OCV Curve:**
   Instead of inaccurate linear interpolation across the long 3.7V LiPo plateau, the driver uses a calibrated piecewise curve:
   | Cell Voltage | SoC (%) |
   | :--- | :--- |
   | $\ge 4180\text{ mV}$ | 100% |
   | $4100\text{ mV}$ | 90% |
   | $4000\text{ mV}$ | 80% |
   | $3900\text{ mV}$ | 70% |
   | $3830\text{ mV}$ | 60% |
   | $3780\text{ mV}$ | 50% |
   | $3740\text{ mV}$ | 40% |
   | $3700\text{ mV}$ | 30% |
   | $3650\text{ mV}$ | 20% |
   | $3580\text{ mV}$ | 10% |
   | $3450\text{ mV}$ | 5% |
   | $\le 3200\text{ mV}$ | 0% |
3. **Operational State Detection:**
   * **`NOT_PRESENT`**: $V_{\text{pin}} < 500\text{ mV}$ (console running on USB with battery disconnected).
   * **`CHARGING`**: Sustained positive rate of change ($dV/dt > 0$) while charging via TP4056.
   * **`FULL`**: $V_{\text{bat}} \ge 4180\text{ mV}$ and stable.
   * **`DISCHARGING`**: Normal battery-powered operation.
4. **Anti-Flicker Monotonicity:** Prevents SoC percentage from jumping back up during brief CPU idle intervals while discharging.

### Battery IOCTL Interface

| IOCTL Command | Code | Argument Type | Description |
| :--- | :---: | :--- | :--- |
| `EOS_POWER_IOCTL_GET_BATTERY_INFO` | `650` | `eos_battery_info_t *out_info` | Retrieves cell millivolts, SoC percentage, charger state, and thresholds. |

```c
typedef enum {
  EOS_BATTERY_STATUS_NOT_PRESENT = 0,
  EOS_BATTERY_STATUS_DISCHARGING = 1,
  EOS_BATTERY_STATUS_CHARGING    = 2,
  EOS_BATTERY_STATUS_FULL        = 3
} eos_battery_status_t;

typedef struct {
  uint32_t voltage_mv;         // Measured/smoothed battery cell voltage in mV
  int32_t  soc_percent;        // State of Charge (0 - 100%, or -1 if not connected)
  eos_battery_status_t status; // Battery operational status
  uint32_t v_empty_mv;         // Nominal empty threshold in mV (e.g. 3200)
  uint32_t v_full_mv;          // Nominal full threshold in mV (e.g. 4200)
} eos_battery_info_t;
```

### VFS Text Interface
Reading `/dev/battery0` outputs human and machine-parsable properties:
```
present: 1
status: discharging
percentage: 91%
voltage_mv: 4095
cell_mv: 4095
```
*(Queried natively by the `/bin/battery` user utility).*
