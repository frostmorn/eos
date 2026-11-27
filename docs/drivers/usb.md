# USB Subsystem, Composite Gadget & Storage Drivers (MTP / MSC)

EOS implements a unified, CherryUSB-backed USB Device & Host subsystem under `main/edriver/bus/usb/` and `main/edriver/usb/`. Hardware controllers are exposed as bus nodes (`/dev/usb0`, `/dev/usb1`), while protocol functions attach as child devices under the USB bus.

---

## 1. Supported USB Drivers (`main/edriver/bus/usb/` & `main/edriver/usb/`)

| Driver | Device Node | Role / Scope | Description |
| :--- | :--- | :--- | :--- |
| **`bus/usb`** | `/dev/usb0`, `/dev/usb1` | `bus` | Manages DWC2 USB OTG controllers (`usb0` FS on ESP32-S3/P4, `usb1` HS on ESP32-P4), composite descriptors, endpoint FIFO allocation, and USB Host mode. |
| **`usb/serial`** | `/dev/usb_serial0` | `usb` | CDC-ACM virtual serial port (`VFS` read/write, supports interactive `ush -i` shell on ESP32-S3). |
| **`usb/modem`** | `/dev/usb_modem0` | `usb` | CDC-ACM AT command modem emulator (`ATI`, `AT+CSQ`, `ATD*99#`, `+++`, `ATH`). |
| **`usb/msc`** | `/dev/usb_msc0` | `usb` | USB Mass Storage Class (Bulk-Only Transport) bridging a block device (e.g. `/dev/sd0`) to the host. |
| **`usb/mtp`** | `/dev/usb_mtp0` | `usb` | Open-source POSIX Media Transfer Protocol (MTP) responder exporting Unified Storage (`/`) or a configured VFS subtree. |

---

## 2. Composite Gadget & Hardware FIFO Constraints

In `DEVICE_COMPOSITE` mode, `bus/usb` dynamically constructs USB configuration descriptors based on the active function bitmask (`EOS_USB_FUNC_SERIAL`, `EOS_USB_FUNC_MODEM`, `EOS_USB_FUNC_MSC`, `EOS_USB_FUNC_MTP`) and MAC-derived unique USB `iProduct` / `iSerialNumber` strings (allowing multiple EOS boards to enumerate simultaneously on the same host without GVFS/libmtp collisions).

### ESP32-S3 4-TX-FIFO Limit vs ESP32-P4 HS OTG
* **ESP32-S3 (Full-Speed OTG):** Hardware provides 4 IN (TX) FIFOs (`0x81..0x84`). When MTP or MSC storage is active, `bus/usb` maps CDC Serial (`0x81`/`0x82`), Storage Bulk IN (`0x83`), Storage Bulk OUT (`0x02`), and MTP Interrupt IN (`0x84`), automatically omitting the secondary CDC Modem interface when all 4 TX FIFOs are required.
* **ESP32-P4 (High-Speed OTG `/dev/usb1`):** Provides 16 endpoints and an 896-word DFIFO, enabling simultaneous Serial + Modem + MTP/MSC composite operation.

### Exclusive Storage Mode (MTP vs MSC)
MTP and MSC share the storage slot (`EOS_USB_FUNC_MTP` vs `EOS_USB_FUNC_MSC` — mutually exclusive):
* **MTP (`usbctl storage mtp /`)**: Exposes the live VFS tree (`/`, `/proc`, `/bin`, `/tmp`, `/sdcard`) via POSIX `opendir`/`readdir`/`open`/`read`/`write` so host and target can access files concurrently without block-level filesystem corruption.
* **MSC (`usbctl storage msc /dev/sd0`)**: Exposes raw block sectors of `/dev/sd0` to the host.

---

## 3. MTP Responder Architecture (`main/edriver/usb/mtp/`)

* **Protocol Support:** PTP/MTP session management, `GetDeviceInfo`, `GetStorageIDs`, `GetStorageInfo`, `GetObjectHandles`, `GetObjectInfo`, `GetObject`, `GetPartialObject` (`0x101B` for GIO/GVFS chunked reads), `SendObjectInfo`/`SendObject`, `DeleteObject`, `MoveObject`, `CopyObject`, and `USBD_EVENT_SET_INTERFACE` handling.
* **Memory & SRAM Efficiency:** Object handle tables use `kvec_psram_t` (`MALLOC_CAP_SPIRAM`), and DMA bounce/stream buffers are sized strictly to hardware transfer requirements (`EOS_USB_MTP_BUF_SIZE = 1024`, `EOS_USB_CDC_BUFFER_SIZE = 256` on ESP32-S3 / `512` on ESP32-P4).

---

## 4. Driver Dependencies & Versions

| Component / Dependency | Version | Used By | Purpose |
| :--- | :---: | :--- | :--- |
| **ESP-IDF** | `6.0.2` | All USB drivers | DWC2 OTG HAL, FreeRTOS primitives, VFS/eFuse MAC APIs |
| **`cherry-embedded/cherryusb`** | `1.6.1` | `bus/usb`, `usb/msc`, `usb/mtp` | Composite USBD stack, CDC-ACM, MSC BOT, and DWC2 host/device DCD |
| **`espressif/tinyusb`** | `0.21.0~6` | Optional / legacy target config | TinyUSB core component |
| **`espressif/esp_tinyusb`** | `2.4.0` | Optional / legacy target config | ESP-IDF TinyUSB wrapper |

---
*Last updated: 2026-11-27*
