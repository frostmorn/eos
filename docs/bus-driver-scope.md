# Bus Driver Scope & Architecture

A bus in EOS is any device whose driver has `scope == "bus"`. This represents a topological role rather than a strict claim about physical hardware. For instance, a composite USB device is a single physical peripheral but acts logically as a bus because it serves as the origin point for its own interfaces, registering under the `bus` scope similarly to SPI or GPIO. The `root` driver is a special case with no underlying hardware, acting as the root anchor of the device tree.

Only bus-scoped devices can parent child devices. `eos_dev_attach` validates `parent->driver->is_bus` before proceeding.

---

## 1. Implemented Bus Drivers (`edriver/bus/`)

EOS includes robust implementations for core hardware and virtual buses under `main/edriver/bus/`:
*   **Root (`root.c`):** The foundational anchor bus representing the top of the device tree (`eos_devtree_root()`).
*   **SPI (`spi.c`):** Serial Peripheral Interface bus managing SPI host controllers, chip selects, clock modes, and child peripheral attachment.
*   **I2C (`i2c.c`):** Inter-Integrated Circuit bus managing I2C controllers, master/slave transactions, and sensor/codec attachment.
*   **SDIO (`sdio.c`):** Secure Digital Input Output bus managing high-speed card communication and SDIO peripheral attachments.
*   **USB (`usb.c`):** Universal Serial Bus interface managing USB host/device endpoints and composite interfaces.
*   **GPIO (`gpio.c`):** General Purpose Input/Output bus managing pin multiplexing, interrupt lines, and discrete device/keyboard attachment.
*   **1-Wire (`onewire.c`):** Single-wire bidirectional serial bus supporting multi-drop bus communication and sensor polling.

---

## 2. Attachment & Permissions

Before linking a child device into the tree, EOS invokes the parent bus's `ioctl` with `EOS_BUS_IOCTL_KID_ATTACH` (defined in `bus.h`). 
*   **Declining Attachment:** The bus can reject attachment (e.g., when a GPIO bus verifies whether a requested pin is already claimed) by returning a non-zero error code (setting `errno` and returning `-1`). The attach operation aborts and the child device never enters the device tree.
*   **Accepting Attachment:** A bus with nothing to validate (such as `root`) must explicitly return `0` on success.
*   **Indexing:** Upon successful attachment, the bus may assign the child an index via the ioctl out-parameter (`child->index`), which remains stable for the child's lifetime. Buses that do not require indexing return `0`.

`EOS_BUS_IOCTL_KID_DETACH` is notification-only; a bus may veto an attachment request but cannot veto a detachment.

---

## 3. Bus Locking & Concurrency

Buses where children share a single physical communication channel that cannot be accessed concurrently (such as SPI or I2C) must implement `EOS_BUS_IOCTL_LOCK` and `EOS_BUS_IOCTL_UNLOCK`. 
*   Child drivers on such a bus must acquire the lock prior to performing `read`/`write` operations and release it afterward.
*   Buses without concurrency restrictions (such as GPIO or root) implement both lock and unlock as no-ops returning `0`, enabling device drivers to invoke locking functions unconditionally regardless of the underlying bus type.

---

## 4. Reserved IOCTL Range

Commands below `EOS_BUS_IOCTL_USER_BASE` (defined in `bus.h`) are framework-reserved and apply universally to all buses. Bus-specific commands (such as SPI clock modes, bit orders, or GPIO directions) must start at or above `EOS_BUS_IOCTL_USER_BASE`.

---

## 5. Adding a New Bus Driver

1.  **Scaffolding:** Run `make driver` (via `tools/codegen.py`, see [Development Guide](../DEVELOPMENT.md)) to generate driver templates under `main/edriver/bus/`.
2.  **Guarding:** Guard the implementation with an appropriate build configuration macro (e.g., `EOS_DRIVER_BUS_<NAME>_ENABLED`).
3.  **Registration:** Register the driver using `EOS_DRIVER_REG(bus, <name>, priority)`. 
4.  **Boot Order:** Buses required by other peripherals at early boot stages must register with appropriate initialization priorities (see `initprio.h`).

---
[Back to Documentation Index](./Readme.md) | [See Display Drivers](./display-scope.md)
