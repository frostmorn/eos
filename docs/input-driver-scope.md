# Input Driver Scope & Event Architecture

Input drivers in EOS (`edriver/input/`) capture user interaction events—such as physical button presses, directional pads, keyboards, and touchscreens—and expose them via standard VFS file nodes (such as `/dev/keyboard0`) adhering to the Linux input subsystem event model.

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

## 2. Keyboard & Keypad Driver (`edriver/input/keyboard/keyboard.c`)

The keyboard driver attaches as a child node to the GPIO bus, monitoring configured input pins (e.g., D-pad buttons, action buttons).
*   **Active-Low Polling:** Pins are configured as inputs with internal pull-ups enabled (`GPIO_PULLUP_ENABLE`), triggering press states when pulled low (`0`).
*   **Blocking & Non-Blocking Read:** Reading from a keyboard device node (`read()`) blocks until a new key press transition is detected or returns an event struct.
*   **IOCTL Polling (`EOS_KEYBOARD_IOCTL_POLL`):** Applications or compatibility layers (such as SDL backend event pumps) can query the keyboard non-blockingly using `ioctl(fd, EOS_KEYBOARD_IOCTL_POLL, &ev)`.

---

## 3. Adding a New Input Driver

1.  **Placement:** Create the implementation under `main/edriver/input/<name>/<name>.c`.
2.  **Guarding:** Guard with a build macro (e.g., `EOS_DRV_INPUT_<NAME>_ENABLED`).
3.  **Registration:** Register using `EOS_DRIVER_REG(input, <name>, priority)`.
4.  **Pin Mapping:** Accept pin definitions via `dev->pins` and claim capability pins through `eos_cap_claim()`.

---
[Back to Documentation Index](./Readme.md)
