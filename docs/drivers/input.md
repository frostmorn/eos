# Input Driver Contracts & Event Architecture

Input drivers in EOS reside under `scope == "input"`, exposing standard VFS event streams (such as `/dev/keyboard0` and `/dev/input0`) adhering to the standardized event model in `main/ecore/input.h`.

---

## 1. Supported Input Drivers (`main/edriver/input/`)

| Driver Name | Device Node | Input Technology | Target Hardware |
| :--- | :--- | :--- | :--- |
| **`gpio_keyboard`** | `/dev/keyboard0` | Discrete hardware button matrix with active-low polling & synthetic FIFO event injection | Lilka v2.3, JC4880P4, QEMU |
| **`gt911`** | `/dev/input0` (`/dev/touch0`) | Capacitive multi-touch panel over I2C | Guition JC4880P443C |

---

## 2. Standardized Event Data Model (`ecore/input.h`)

All input drivers communicate using the standardized `eos_input_event_t` structure:

```c
typedef enum {
  EOS_INPUT_EVENT_KEYBOARD = 0,
  EOS_INPUT_EVENT_TOUCH    = 1
} eos_input_event_type_t;

typedef struct {
  uint16_t type;  // EOS_INPUT_EVENT_KEYBOARD or EOS_INPUT_EVENT_TOUCH
  union {
    uint16_t code; // Linux input keycode (e.g. KEY_ENTER, KEY_UP, KEY_A)
    struct {
      uint16_t x;  // Absolute screen X coordinate
      uint16_t y;  // Absolute screen Y coordinate
    };
  };
} eos_input_event_t;
```

### Standard Linux Input Keycodes:
Drivers map hardware keypad lines to canonical key identifiers:
* `KEY_UP` (103), `KEY_DOWN` (108), `KEY_LEFT` (105), `KEY_RIGHT` (106)
* `KEY_ENTER` (28, Select / Action)
* `KEY_SPACE` (57, Start / Menu)
* `KEY_A` (30), `KEY_B` (48), `KEY_C` (46), `KEY_D` (32)
* `KEY_ESC` (1, Back / Escape)
* `KEY_SLEEP` (142, Power / Sleep)

---

## 3. VFS Contract & Concurrency

### Reading Events (`read`)
* Applications read full event structures: `read(fd, &event, sizeof(eos_input_event_t))`.
* **Blocking Semantics:** If no events are queued, `read()` suspends the calling thread on an internal FreeRTOS counting semaphore. When a hardware key or touch coordinate occurs, the semaphore wakes the reader with zero CPU spinning.
* Returns `sizeof(eos_input_event_t)` (4 bytes) per event.

### Non-Blocking Polling (`ioctl`)
For game loops or UI engines that cannot block:

| IOCTL Command | Numeric Code | Argument Type | Description |
| :--- | :---: | :--- | :--- |
| `EOS_KEYBOARD_IOCTL_POLL` | `300` | `eos_input_event_t *out_ev` | Polls event queue non-blocking. Returns `0` if event retrieved, `-1` with `EWOULDBLOCK` if empty. |

### Synthetic Event Injection (`write`)
Any process can inject synthetic input events by writing to `/dev/keyboard0`:
1. **Binary Injection:** Write a raw `eos_input_event_t` structure.
2. **ASCII Name Injection:** Write a human-readable key name string (e.g., `"up\n"`, `"enter\n"`, `"esc\n"`, `"a\n"`). The driver automatically parses the string, translates it to the corresponding keycode, and enqueues it.

This capability enables headless test runners (`tools/hardware_test.py`) to automate full GUI and shell validation across QEMU and physical devices.

---

## 4. Declarative Attachment Examples

```sh
# Lilka v2.3: Discrete keypad with active-low pull-ups
devctl attach input gpio_keyboard gpio \
  --pin up=14,down=13,left=12,right=11,a=4,b=5,c=6,d=40,select=9,start=10,esc=0

# Guition JC4880P4: GT911 Capacitive Touch over I2C bus 0
devctl attach input gt911 i2c0 --pin rst=23,irq=22 --cfg addr=0x5d,width=480,height=480

# QEMU Target: Virtual event injection keyboard (no pins attached)
devctl attach input gpio_keyboard gpio
```
