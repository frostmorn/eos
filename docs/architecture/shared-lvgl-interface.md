# Shared LVGL Interface Layer (`ecore/lvgl`)

The EOS Shared LVGL Interface Layer (`main/ecore/lvgl.c`, `main/ecore/lvgl.h`) serves as the central graphics and user-interaction subsystem of the operating system. It bridges low-level display and input hardware drivers (`/dev/fb*`, `/dev/vfb*`, `/dev/keyboard*`, `/dev/touch*`) with user-space GUI applications (`ebin/launcher`, `ebin/lvgl_net`, `ebin/lvgl_term`, `ebin/lvgl_calc`, etc.).

---

## 1. Architectural Role & Principles

In accordance with EOS architectural rules:

1. **POSIX & VFS Encapsulation:** GUI applications in user space (`main/ebin/`) interact with hardware devices exclusively through standard file descriptors (`/dev/fb*`, `/dev/vfb*`, `/dev/keyboard*`, `/dev/touch*`) and the shared LVGL subsystem. Applications are strictly forbidden from invoking vendor hardware APIs or FreeRTOS primitives directly.
2. **Device Independence:** Applications do not hardcode screen dimensions, pixel formats, or input controller types. The LVGL subsystem automatically detects display resolution, memory limits, and attached input devices (keypad, touchscreen, or both).
3. **Compositor & Multi-Window Awareness:** The interface layer transparently interfaces with the display compositor (`edriver/display/compositor`). Window boundaries and vertical offsets are queried automatically, mapping touch coordinates and draw areas seamlessly.
4. **Lifecycle & Resource Ownership:** Each LVGL session is bound to the calling process (`owner_pid`). Process exit automatically triggers cleanup hooks registered via `eos_proc_on_exit()`, preventing orphaned framebuffer mappings, memory leaks, or locked input devices.

---

## 2. Subsystem Architecture

```
+-------------------------------------------------------------------------+
|                  EOS User-Space Applications (ebin/)                    |
|       launcher    lvgl_net    lvgl_term    lvgl_calc    lvgl_snake      |
+-------------------------------------------------------------------------+
                                    |
      Shared Dialogs / Modals / Navigation / Statusbar & Toolbar APIs
                                    v
+-------------------------------------------------------------------------+
|              EOS LVGL Subsystem Layer (main/ecore/lvgl)                 |
|  - Process lifecycle & auto-reclaim    - Key mapping & queueing         |
|  - Framebuffer DMA strip pipeline      - Touch sampling & offset mapping|
|  - Shared Keyboard & Dialog Engine     - Atomic status/toolbar IPC      |
+-------------------------------------------------------------------------+
             |                                             |
             v                                             v
+-----------------------------+               +---------------------------+
| Display Driver / Compositor |               |   Input Drivers (/dev)    |
| /dev/fb0, /dev/vfb0 (main)  |               |  /dev/keyboard0 (GPIO)    |
| /dev/vfb1 (status), vfb2    |               |  /dev/touch0 (GT911 I2C)  |
+-----------------------------+               +---------------------------+
```

---

## 3. Initialization & Process Lifecycle

### 3.1 Session Creation
An application initializes the LVGL subsystem using either declarative configuration or path-based convenience functions:

```c
// Full configuration structure
eos_lvgl_cfg_t cfg = {
    .fb_path = "/dev/vfb0",          // Framebuffer or compositor device
    .kbd_path = "/dev/keyboard0",    // Keypad device (or NULL for default)
    .touch_path = "/dev/touch0",     // Touch controller (or NULL)
    .enable_keyboard = true,
    .disable_touch = false,
    .dpad_group_nav = true,          // D-Pad navigates focus groups
};
if (eos_lvgl_init(&cfg) != 0) {
    /* handle failure */
}

// Convenience initialization using defaults or CLI parameters:
if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    /* handle failure */
}
```

### 3.2 Display Resolution & Device Query
Upon opening the target framebuffer file descriptor, `eos_lvgl_init()` queries display parameters via standard IOCTLs:
* `EOS_DISPLAY_IOCTL_GET_WIDTH` & `EOS_DISPLAY_IOCTL_GET_HEIGHT`: Resolves viewport geometry.
* `EOS_DISPLAY_IOCTL_GET_OFFSET_X` & `EOS_DISPLAY_IOCTL_GET_OFFSET_Y`: Resolves compositor window origins.
* `EOS_LVGL_DMA_STRIP_BYTES` (16 KB): Computes optimal partial draw strip height (`partial_lines`), conserving internal DMA-capable SRAM while optimizing SPI/DPI scanout.

### 3.3 Automatic Process Teardown
During `eos_lvgl_init()`, an exit hook is registered with the kernel:
```c
eos_proc_on_exit(lvgl_proc_cleanup_hook, NULL, "lvgl_deinit");
```
When a GUI application exits (normally via `exit()` or abnormally), the kernel executes `eos_lvgl_deinit()`, which safely frees draw buffers, closes open file descriptors (`fb_fd`, `kbd_fd`, `touch_fd`), and deletes active LVGL display and indev objects.

---

## 4. Display & Compositor Pipeline

### 4.1 Invalidation Area Rounding
To prevent pitch and stride distortion on displays with hardware alignment requirements (e.g., ST7789, ST7701S), `eos_lvgl_invalidate_area_cb()` horizontally expands invalidated regions to the full display width:
```c
area->x1 = 0;
area->x2 = (int32_t)ctx->width - 1;
```
This guarantees that draw-buffer rows always align cleanly to `width * sizeof(uint16_t)` (RGB565).

### 4.2 Full Fallback Mode
For benchmark comparisons and diagnostic rendering, full-frame fallback rendering is supported:
* `eos_lvgl_set_full_fallback(bool enable)`: Toggle between DMA partial strips and shadow full-frame buffers.
* `eos_lvgl_is_full_fallback()`: Query active rendering mode.
* `eos_lvgl_flush_full_frame()`: Force an immediate scanout commit of the entire screen buffer.

---

## 5. Input Subsystem Integration

### 5.1 Unified Key Architecture
Hardware input devices emit raw `eos_input_event_t` packets. The LVGL layer transforms these into standardized navigation and text-entry events:

| Hardware Action | EOS Key Code | `eos_lvgl_map_key()` | Context: Main / Form | Context: Active Keyboard / Input |
|---|---|---|---|---|
| **D-Pad Up** | `KEY_UP` | `LV_KEY_UP` / `LV_KEY_PREV` | Navigate widgets up / prev | Move selection cursor up |
| **D-Pad Down** | `KEY_DOWN` | `LV_KEY_DOWN` / `LV_KEY_NEXT` | Navigate widgets down / next | Move selection cursor down |
| **D-Pad Left** | `KEY_LEFT` | `LV_KEY_LEFT` / `LV_KEY_PREV` | Navigate widgets left / prev | Move selection cursor left |
| **D-Pad Right** | `KEY_RIGHT` | `LV_KEY_RIGHT` / `LV_KEY_NEXT` | Navigate widgets right / next | Move selection cursor right |
| **Action Button (A)** | `KEY_ENTER` / `KEY_A` | `LV_KEY_ENTER` | Click / Activate focused widget | Insert selected letter / Submit |
| **Back Button (B)** | `KEY_BACKSPACE` / `KEY_B` | `LV_KEY_BACKSPACE` | Exit / Return to previous view | Delete character (Backspace) |
| **Cancel / Escape** | `KEY_ESC` | `LV_KEY_ESC` | Dismiss dialog / Cancel / Exit | Dismiss input dialog |
| **Aux Button (C)** | `KEY_C` | `LV_KEY_PREV` (or Backspace in edit) | Custom action / Mode toggle | Delete character (Backspace) |

### 5.2 Single-Consumer Key Queue & Indev Coordination
To prevent race conditions between manual event loops (`eos_lvgl_poll_key()`) and LVGL's internal indev driver callback (`eos_lvgl_indev_read_cb()`):
* All hardware keyboard events are read once and ingested into `s_lvgl.key_queue`.
* Active keyboards intercept navigation and action keys atomically before they reach the main event loop.
* Key presses generate synthetic `LV_INDEV_STATE_RELEASED` cycles in subsequent ticks, ensuring LVGL triggers `LV_EVENT_CLICKED` reliably.

### 5.3 Touchscreen Sampling & Compositor Mapping
Touch sampling via `poll_hardware_touch()` executes once per frame using non-blocking IOCTL (`EOS_TOUCH_IOCTL_POLL`):
1. **Single-Sample Discipline:** Drains exactly one event per tick to prevent clearing instantaneous hardware registers (such as GT911 buffer status bits) and causing false release events.
2. **Compositor Coordinate Mapping:** Physical touch coordinates are translated to local application coordinates:
   ```c
   int32_t mx = (int32_t)ev.x - (int32_t)s_lvgl.fb_offset_x;
   int32_t my = (int32_t)ev.y - (int32_t)s_lvgl.fb_offset_y;
   ```
   If an application runs on `/dev/vfb0` at offset `y = 48`, physical point `(100, 48)` maps accurately to `(100, 0)` in application space.
3. **Gestures:** Touch drags are passed directly to LVGL native gesture recognizers (`LV_EVENT_GESTURE`, `LV_DIR_LEFT`, `LV_DIR_RIGHT`) with momentum scrolling enabled.

---

## 6. Shared UI Components & Modal Dialogs

EOS provides built-in, responsive modal components adapted for both compact (280x240) and large (800x480) displays.

### 6.1 On-Screen Keyboard (`eos_lvgl_create_keyboard`)
Creates a centered, responsive virtual keyboard attached to a target `lv_textarea`:
```c
lv_obj_t *eos_lvgl_create_keyboard(lv_obj_t *parent, lv_obj_t *target_ta);
```
* **Dual Input:** Simultaneously supports capacitive touchscreen taps and physical D-Pad/Enter navigation.
* **Keyboard Focus Interception:** When active, physical keys `KEY_UP`/`DOWN`/`LEFT`/`RIGHT` move button focus, `KEY_A`/`KEY_ENTER` triggers the selected key, and `KEY_B`/`KEY_C` deletes characters.

### 6.2 Text Input Dialog (`eos_lvgl_create_input_dialog`)
Opens a modal input box with text prompt, password masking, on-screen keyboard, and OK/Cancel buttons:
```c
typedef struct {
    const char *title;         // Dialog title (e.g. "Connect to Wi-Fi")
    const char *prompt;        // Text input prompt / label
    const char *initial_value; // Initial pre-filled text
    const char *ok_label;      // e.g. "Connect", "Save"
    const char *cancel_label;  // e.g. "Cancel"
    bool is_password;          // Enable bullet masking
    eos_lvgl_input_cb_t cb;    // Callback: void cb(const char *text, void *user_data)
    void *user_data;
} eos_lvgl_input_dialog_cfg_t;

lv_obj_t *eos_lvgl_create_input_dialog(const eos_lvgl_input_dialog_cfg_t *cfg);
```

### 6.3 Confirmation Dialog (`eos_lvgl_dialog_confirm`)
Presents a binary choice modal (Yes/No, Confirm/Cancel):
```c
typedef struct {
    const char *title;
    const char *message;
    const char *confirm_label; // e.g. "Delete"
    const char *cancel_label;  // e.g. "Keep"
    eos_lvgl_confirm_cb_t cb;  // void cb(bool confirmed, void *user_data)
    void *user_data;
} eos_lvgl_confirm_dialog_cfg_t;

lv_obj_t *eos_lvgl_dialog_confirm(const eos_lvgl_confirm_dialog_cfg_t *cfg);
```

### 6.4 Information & Alert Dialogs
* **`eos_lvgl_dialog_info()`**: Presents informational notices with an acknowledge button and optional auto-dismiss timeout (`timeout_ms`).
* **`eos_lvgl_dialog_alert()`**: Displays styled alerts with distinct visual accents based on severity (`EOS_LVGL_ALERT_INFO`, `EOS_LVGL_ALERT_WARNING`, `EOS_LVGL_ALERT_ERROR`).
* **`eos_lvgl_dialog_close(lv_obj_t *dialog)`**: Explicitly dismisses an open modal dialog.

### 6.5 Standard Header Exit Button (`eos_lvgl_create_back_btn`)
Places a standardized `[✕]` close button in the top-right corner of a container or screen:
```c
lv_obj_t *eos_lvgl_create_back_btn(lv_obj_t *parent, lv_event_cb_t custom_cb, void *user_data);
```
If `custom_cb` is `NULL`, clicking the button automatically enqueues `KEY_ESC`, ensuring universal exit behavior across all apps.

### 6.6 Adaptive Menu Subsystem (`eos_open_menu`)
EOS provides a centralized, screen-adaptive menu component that automatically formats selection menus based on the active viewport resolution:

* **Compact Screens (< 400px width/height, e.g. Lilka v2.3):** Renders a headerless, full-width menu (`eos_menu`) to maximize available list space.
* **Large Screens (≥ 400px, e.g. JC4880P4):** Renders a floating modal dialog with title header, close button, and shadow overlay (`eos_popup_menu`).

```c
typedef struct eos_menu_cfg {
  const char *title;              // Menu title (suppressed in headerless mode)
  const char *icon;               // Title icon (Emoji or Symbol)
  const eos_menu_item_t *items;   // Item array
  size_t item_count;
  eos_menu_select_cb_t on_select; // Fallback selection / activation callback
  eos_menu_select_cb_t on_focus;  // Cursor focus change callback
  eos_menu_cancel_cb_t on_cancel; // Dismiss / cancel callback
  void *user_data;
  bool headerless;                // Explicitly hide header (default true on small screens)
  bool force_fullscreen;          // Force full-screen eos_menu even on large screens
  int32_t width;                  // Custom width (0 for default responsive width)
  int32_t height;                 // Custom height (0 for default responsive height)
  lv_obj_t *parent;               // Parent container (NULL for active screen)
} eos_menu_cfg_t;

// Factory: automatically adapts to screen dimensions
lv_obj_t *eos_open_menu(const eos_menu_cfg_t *cfg);

// Menu Focus, Selection & Keypad Arrow Navigation:
int eos_menu_get_focused_index(lv_obj_t *menu);
int eos_menu_get_selected_index(lv_obj_t *menu); // Backwards-compatible alias for get_focused
void eos_menu_focus_index(lv_obj_t *menu, int index);
void eos_menu_select_index(lv_obj_t *menu, int index); // Focuses item and scrolls into view
bool eos_menu_handle_key(lv_obj_t *menu, uint16_t key);
void eos_menu_close(lv_obj_t *menu);
```

#### Focus vs Selection Semantics
EOS GUI menus enforce a clear semantic separation between **Focus** and **Selection**:
1. **Focus (Cursor Highlight)**:
   - Navigating with D-Pad or Arrow keys (`KEY_UP`, `KEY_DOWN`, `LV_KEY_UP`, `LV_KEY_DOWN`) changes the *focused* candidate item.
   - Moving focus updates visual styling (highlight background and focus border) and triggers `cfg->on_focus`, allowing the application to update contextual toolbar text or detail sidebars dynamically without committing to any action.
   - Guaranteed single-focus: navigation strictly maintains exactly one focused element at all times.
2. **Selection (Activation / Multi-Select Commitment)**:
   - Triggered when the user deliberately presses an action key (`KEY_ENTER`, `KEY_A`, `LV_KEY_ENTER`) or taps a touch item.
   - Dispatches `cfg->on_select` or the individual item's `.on_select` callback.
   - Keeping selection conceptually distinct from focus provides a clean foundation for future batch operations (e.g. marking multiple files for batch deletion or copying).

#### Menu Key Navigation Discipline
To maintain consistent navigation across all GUI applications:
1. In the application event loop, when a menu is active, pass input keys through `eos_menu_handle_key(menu, key)`.
2. `eos_menu_handle_key` handles `KEY_UP`, `KEY_DOWN`, `KEY_ENTER`, `KEY_A`, `KEY_ESC`, `KEY_B`, and `KEY_BACKSPACE` automatically:
   - Up/Down updates focus and highlights the active item (`eos_menu_focus_index()`), scrolling the container if needed and invoking `on_focus`.
   - Enter/A triggers the selected item callback or `cfg->on_select`.
   - Esc/B triggers `cfg->on_cancel` or dismisses the menu cleanly.

---

## 7. Statusbar & Toolbar IPC API

EOS applications display persistent contextual info and keyboard shortcut legends via the system statusbar and bottom toolbar. The LVGL subsystem provides an atomic file-backed API using `/tmp/` staging and atomic `rename()`:

### 7.1 Toolbar API
```c
// Set application title and keyboard shortcut legend
void eos_toolbar_set(const char *title, const char *legend);

// Formatted toolbar output
void eos_toolbar_printf(const char *fmt, ...);

// Clear toolbar upon application exit
void eos_toolbar_clear(void);
```

*Example usage:*
```c
eos_toolbar_set("EOS Terminal", "[▲▼]Scroll  [A]Run  [D]Edit  [B]Exit");
```

### 7.2 Statusbar API
```c
// Set system status text
void eos_statusbar_set(const char *status_text);

// Formatted status text output
void eos_statusbar_printf(const char *fmt, ...);

// Clear statusbar override
void eos_statusbar_clear(void);
```

---

## 8. Best Practices for GUI Application Authors

1. **Handle Exit Uniformly:**
   Always handle `KEY_B`, `KEY_BACKSPACE`, and `KEY_ESC` to exit the main loop:
   ```c
   case KEY_B:
   case KEY_BACKSPACE:
   case KEY_ESC:
     app->quit = true;
     break;
   ```
2. **Provide Touch & Physical Navigation Parity:**
   Always instantiate `eos_lvgl_create_back_btn()` in the application header so touchscreen users can exit as easily as keypad users.
3. **Yield to Background System Services:**
   In the main application loop, invoke `eos_lvgl_step(10)` or `usleep(10000)` to advance LVGL animation timers and allow lower-priority background tasks (such as network or audio) to execute.
4. **Clean up on Exit:**
   Call `eos_toolbar_clear()` and `eos_lvgl_deinit()` prior to returning from `main()`.
