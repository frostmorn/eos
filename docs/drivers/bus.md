# Bus Driver Contracts & Architecture

A **bus** in EOS is any device driver declared with `scope == "bus"`. A bus represents a topological role rather than purely physical hardware: it serves as a parent origin point capable of attaching and managing child devices in the EOS Device Tree.

Only drivers with `is_bus == true` can parent child devices. `eos_dev_attach()` enforces this verification before adding nodes to the device hierarchy.

---

## 1. Supported Bus Drivers (`main/edriver/bus/`)

| Bus Driver | Typical Node | Description | Supported Hardware |
| :--- | :--- | :--- | :--- |
| **`root`** | `/dev/root` | Virtual device tree anchor with no underlying hardware. Always instantiated at boot. | Universal (all boards) |
| **`spi`** | `/dev/spi0`, `/dev/spi1` | Manages SPI controller hosts, pin multiplexing, clock configurations, and CS lines. | ESP32-S3 (SPI2/SPI3), ESP32-P4 |
| **`i2c`** | `/dev/i2c0` | Manages I2C master controllers, transaction queuing, clock stretching, and address probing. | ESP32-S3, ESP32-P4 |
| **`sdio`** | `/dev/sdio0`, `/dev/sdio1` | Manages SDMMC host slots. Slot 0 for high-speed storage; Slot 1 for lazy ESP-Hosted co-processor links. | ESP32-P4 (SDMMC controller) |
| **`gpio`** | `/dev/gpio` | Container bus exposing individual GPIO pins as virtual nodes (`/dev/gpio/<pin>`). | Universal |
| **`usb`** | `/dev/usb0` | Manages USB host/device controllers and composite device endpoints. | ESP32-S3 / ESP32-P4 USB OTG |

---

## 2. Bus Attachment & Synchronization Protocol (`main/ecore/bus.h`)

Commands below `EOS_BUS_IOCTL_USER_BASE` (100) are reserved framework commands implemented by all buses:

| IOCTL Command | Numeric Code | Argument Type | Return Value | Purpose |
| :--- | :---: | :--- | :--- | :--- |
| `EOS_BUS_IOCTL_KID_ATTACH` | `0` | `eos_dev_t *child` | `0` on accept, `-1` on veto (`errno`) | Bus validates whether child parameters (pins, speed, address) can be accepted. May assign `child->index`. |
| `EOS_BUS_IOCTL_KID_DETACH` | `1` | `eos_dev_t *child` | `0` | Notification that child is being detached. Detachment cannot be vetoed. |
| `EOS_BUS_IOCTL_LOCK` | `2` | `NULL` | `0` on success, `-1` on error | Acquires exclusive bus hardware lock (e.g. mutual exclusion for SPI/I2C transactions). |
| `EOS_BUS_IOCTL_UNLOCK` | `3` | `NULL` | `0` on success, `-1` on error | Releases exclusive bus hardware lock. |

### Bus Attachment Validation Rules:
* If a bus validates child constraints (such as checking whether a requested GPIO pin or I2C address is already occupied), it sets `errno` (e.g. `EBUSY`, `EINVAL`) and returns `-1`. The attach operation terminates and the child node is never added to the tree.
* Buses without special attachment restrictions (such as `root`) must return `0`.
* Concurrency-free buses (such as `root` or `gpio`) implement `LOCK` and `UNLOCK` as no-ops returning `0`, allowing child drivers to call bus locking unconditionally.

---

## 3. Bus-Specific IOCTL Interfaces

### GPIO Bus IOCTLs (`/dev/gpio/<pin>`)
Exposed when opening individual GPIO pin nodes:

| IOCTL Command | Numeric Code | Argument | Description |
| :--- | :---: | :--- | :--- |
| `EOS_GPIO_IOCTL_SET_FLOATING` | `100` | `none` | Disables internal pull-up and pull-down resistors. |
| `EOS_GPIO_IOCTL_SET_PULLUP` | `101` | `none` | Enables internal pull-up resistor. |
| `EOS_GPIO_IOCTL_SET_PULLDOWN` | `102` | `none` | Enables internal pull-down resistor. |
| `EOS_GPIO_IOCTL_SET_DIRECTION`| `103` | `int dir` (0=in, 1=out) | Configures pin direction mode. |
| `EOS_GPIO_IOCTL_GET_DIRECTION`| `104` | `int *out_dir` | Queries active pin direction mode. |
| `EOS_GPIO_IOCTL_READ` | `105` | `int *out_level` | Queries digital logic level (0 or 1). |
| `EOS_GPIO_IOCTL_WRITE` | `106` | `int level` (0 or 1) | Drives digital output level. |

### I2C Bus IOCTLs (`/dev/i2c*`)

| IOCTL Command | Numeric Code | Argument | Description |
| :--- | :---: | :--- | :--- |
| `EOS_I2C_IOCTL_GET_PINS` | `120` | `int pins[2]` (out: `[0]=sda`, `[1]=scl`) | Queries GPIO pin numbers configured for SDA and SCL. |
| `EOS_I2C_IOCTL_TRANSACT` | `121` | `eos_i2c_msg_t *msg` | Executes master write-then-read transaction on the bus. |
| `EOS_I2C_IOCTL_SCAN` | `122` | `eos_i2c_scan_t *scan` | Probes all 7-bit addresses (0x03..0x77) and returns detected ACK list. |

### SDIO Bus IOCTLs (`/dev/sdio*`)

| IOCTL Command | Numeric Code | Argument | Description |
| :--- | :---: | :--- | :--- |
| `EOS_SDIO_IOCTL_CMD52` | `140` | `eos_sdio_cmd52_t *cmd` | Direct single-byte register read/write via SDIO Command 52. |
| `EOS_SDIO_IOCTL_CMD53` | `141` | `eos_sdio_cmd53_t *cmd` | Block/byte multi-data transfer via SDIO Command 53. |
| `EOS_SDIO_IOCTL_RESET` | `142` | `none` | Re-initializes SDIO host controller interface. |

---

## 4. Attachment Syntax & Declarative Configuration

Buses are typically brought up in `/bin/init.sh` via `devctl attach`:

```sh
# 1. Bring up SPI host 1 on Lilka v2.3
devctl attach bus spi root --pin sclk=18,mosi=17,miso=8 --cfg host=1,dma=3

# 2. Bring up I2C bus 0 on Lilka v2.3
devctl attach bus i2c root --pin sda=10,scl=11 --cfg port=0,freq=400000

# 3. Bring up SDMMC Slot 1 on ESP32-P4 for ESP-Hosted co-processor
devctl attach bus sdio root --pin clk=18,cmd=19,d0=14,d1=15,d2=16,d3=17 --cfg slot=1,width=4,freq=40000000,lazy=1
```
