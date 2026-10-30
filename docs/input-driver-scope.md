# Input Driver Scope & Event Architecture

Input drivers in EOS (`edriver/input/`) capture user interaction events—such as physical button presses, directional pads, keyboards, and capacitive touchscreens—and expose them via standard VFS file nodes (such as `/dev/keyboard0`, `/dev/input0`) adhering to the Linux input subsystem event model.

---

## 1. Event Data Model (`input.h`)

All input events are structured using the standardized `eos_input_event_t` record format:

```c
typedef enum {
  EOS_INPUT_EVENT_KEYBOARD,
  EOS_INPUT_EVENT_TOUCH
} eos_input_event_type_t;

typedef struct {
  uint16_t type;
  union {
    uint16_t code; // Key code (e.g. KEY_ENTER, KEY_UP, KEY_ESC)
    struct {
      uint16_t x;
      uint16_t y;
    };
  };
} eos_input_event_t;
```

*   **Keyboard Events:** `type == EOS_INPUT_EVENT_KEYBOARD`, carrying standard Linux input key codes (`KEY_A`, `KEY_ESC`, `KEY_UP`, `KEY_DOWN`, `KEY_ENTER`, etc., defined in `input.h`).
*   **Touch Events:** `type == EOS_INPUT_EVENT_TOUCH`, carrying absolute 2D screen coordinates `(x, y)`.

---

## 2. Implemented Input Drivers (`edriver/input/`)

*   **GPIO Keyboard (`gpio_keyboard/`):** Unified keyboard driver supporting both discrete hardware button monitoring and synthetic event injection (`devctl attach input gpio_keyboard gpio [--pin ...] [--cfg ...]`). When `--pin` is provided, it monitors configured input pins with active-low polling and pull-ups enabled; when attached without pins (e.g. on `esp32s3_qemu`), it operates as a virtual event FIFO keyboard. Supports synthetic event injection via `write()` (binary `eos_input_event_t` or key names like `"up"`, `"a"`, `"esc"`), FreeRTOS counting semaphore synchronization, blocking `read()`, and non-blocking `EOS_KEYBOARD_IOCTL_POLL`. Exposes `/dev/keyboard0`.
*   **GT911 Touchscreen (`gt911/`):** Capacitive touch controller driver communicating over I2C to report multi-touch absolute coordinates (`EOS_INPUT_EVENT_TOUCH`). Exposes `/dev/input0` (with `/dev/touch0` compatibility symlink).

---

## 3. Adding a New Input Driver

1.  **Placement:** Create the implementation under `main/edriver/input/<name>/<name>.c`.
2.  **Guarding:** Guard with a build macro (e.g., `EOS_DRV_INPUT_<NAME>_ENABLED`).
3.  **Registration:** Register using `EOS_DRIVER_REG(input, <name>, priority)`.
4.  **Pin Mapping:** Accept pin definitions via `dev->pins` and claim capability pins through `eos_cap_claim()`.

---
[Back to Documentation Index](./Readme.md)
