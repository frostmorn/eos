# EOS Development Guidelines & Architecture Specifications

This document defines the architectural rules, coding standards, and development workflows for the EOS (ESP Operating System) codebase. All developers and automated agents must adhere to these specifications.

---

## 1. GUI Application Architecture & Logic Separation

All graphical applications (`EOS_BIN_FLAG_GUI` binaries in `main/ebin/lvgl_*`) must adhere to strict separation between presentation and business logic:

### 1.1 File Structure Standard
Every GUI application directory must be split into at least two separate units:
* **`<app>_logic.c` / `<app>_logic.h` (Business Logic Engine)**:
  - Defines the core data structures, state machines, and functional operations.
  - Controls application workflow, I/O, parsing, network, and storage logic.
  - **Zero GUI/LVGL Dependencies**: Must **NEVER** include `lvgl.h`, `ecore/lvgl.h`, or any UI presentation headers.
  - Must be 100% testable headlessly in automated test harnesses without initializing a screen or display task.
* **`<app>.c` / `<app>_ui.c` (Presentation Layer)**:
  - Consumes `<app>_logic.h` to render visual representations.
  - Listens to user inputs (keyboard, touch, navigation) and invokes logic functions.
  - Uses unified EOS UI widgets (`eos_menu`, `eos_popup_menu`, `eos_toolbar`).

### 1.2 Mandatory Automated Testing
* Every business logic module (`*_logic.c`) must be covered by automated headless unit tests in `tools/hardware_test.py`.
* Tests must run non-interactively with bounded timeouts and zero user attention.

---

## 2. Adaptive Screen Scaling & Unified Menus

EOS supports diverse screen form factors ranging from compact handhelds (e.g. Lilka v2.3 with 280x240 display) to larger high-resolution panels (e.g. Guition JC4880P4 with 480x800 display).

### 2.1 Screen-Adaptive Menu Paradigm
* **Never use fixed-size popup windows or hardcoded modal dialogs** across different devices.
* All contextual options, dialogs, and selection lists must use `eos_open_menu()`:
  - **Small Screens (`width <= 320` or `height <= 240`)**:
    - Automatically opens `eos_menu` — a clean, dedicated full-screen or full-area responsive list.
    - **Headerless Mode**: Static headers are omitted on small screens by default to prevent wasting vertical pixels; navigation context is embedded directly in the first list item (e.g. `📁 .. [/bin]`) or in the toolbar.
  - **Large Screens (`width > 320` and `height > 240`)**:
    - Automatically opens `eos_popup_menu` — a centered, compact modal overlay with zero padding/gap between buttons and a translucent backdrop.

---

## 3. Lightweight Layouts (Prohibition of Heavy `lv_flex` in Lists)

* **Avoid `lv_flex` on Dynamic Lists**:
  - `lv_flex` calculates tracks dynamically during layout passes, recursively allocating memory pool scratch arrays (`grow_dsc_t`).
  - In lists with dozens or hundreds of items, flex layout incurs high CPU latency and can exhaust the LVGL memory pool.
* **Lightweight Alternative**:
  - Use coordinate-based linear stacking (`LV_LAYOUT_NONE` with explicit Y positioning `y = i * item_height`).
  - Use `eos_menu`, which natively manages lightweight linear list layouts without flex recursion.

---

## 4. PSRAM-Backed Dynamic Collections (`kvec_psram_t`)

* **No Fixed-Size Static Array Limits**:
  - Dynamic datasets (file listings, process tables, network scans) must not use hardcoded static arrays or stack buffers.
* **Dynamic PSRAM Allocation**:
  - Use `kvec_psram_t` (`emisc/kvec.h`) for all dynamic collections, allocating out of the 32 MB external PSRAM to protect internal SRAM from fragmentation.
* **Minimal Entry Memory Footprint**:
  - Store relative filenames and complete metadata directly via `struct stat` in each entry (e.g. `fm_entry_t`). Full paths are deduced on demand using `getcwd()` or `chdir()`, avoiding redundant string storage per entry.

---

## 5. Rich Text Styling: UTF-8 Emojis & ANSI Color Codes

* **Glyphs & Icons**:
  - Prefer standard UTF-8 emojis (`📁`, `📄`, `⚙️`, `🗑️`, `▶️`, `👁️`, `➕`, `↩️`) over private font symbol definitions for platform-wide glyph compatibility.
* **Colors**:
  - Text strings support standard ANSI escape sequences (`\033[32m`, `\033[33m`, `\033[0m`) and LVGL inline recolor syntax (`#RRGGBB text#`), parsed automatically by `eos_ansi_to_lvgl_recolor()`.

---

## 6. Automated Testing & Fail-Fast Execution

* **Mandatory Fail-Fast Testing (`-x` / `--failfast`)**:
  - Both `tools/layer_audit.py` and `tools/hardware_test.py` support the fail-fast flag (`-x` or `--failfast`) to halt execution immediately on the first failure. This drastically speeds up iteration cycles and root-cause analysis:
    ```bash
    python3 tools/layer_audit.py -x
    python3 tools/hardware_test.py -p /dev/ttyACM0 --suite all -x
    ```
  - **Zero Regressions**: All modifications must pass with 0 failures on physical target devices (ESP32-P4 and ESP32-S3) and QEMU.

---

## 7. Centralized GUI Navigation, Menus, Dialogs, and Keyboards

All GUI applications (`main/ebin/lvgl_*`) must use centralized EOS LVGL kernel abstractions:
* **Menus & Selection Lists**:
  - Must use `eos_open_menu()`, `eos_menu_create()`, or `eos_popup_menu_create()`.
  - Full responsive width: menus on small screens automatically occupy 100% of available screen width (`lv_pct(100)`).
  - Focus vs Selection: Arrow/D-pad navigation updates *focus* (`eos_menu_focus_index()`, triggering `cfg->on_focus`), visually highlighting exactly one candidate item with `EOS_THEME_PRIMARY_BLUE` and scrolling into view. Deliberate activation (Enter/A key or touch tap) triggers *selection* (`cfg->on_select`), preserving architectural readiness for future multi-file batch operations.
  - Keyboard/D-pad navigation: apps must delegate keypad arrow and confirmation events to `eos_menu_handle_key()`.
* **Modal Dialogs (Alerts, Confirms, Info)**:
  - Must use `eos_lvgl_dialog_confirm()`, `eos_lvgl_dialog_alert()`, or `eos_lvgl_dialog_info()` instead of ad-hoc custom message boxes or `lv_msgbox_create()`.
* **Virtual Keyboards & Text Input**:
  - Must use `eos_lvgl_create_input_dialog()` or `eos_lvgl_create_keyboard()` instead of ad-hoc virtual keyboards or `lv_keyboard_create()`.

---

## 8. Memory Pool & PSRAM Allocation Guidelines

* **LVGL Pool Sizing (`CONFIG_LV_MEM_SIZE`)**:
  - Complex multi-process GUI environments running concurrent background daemons (e.g. `launcher` alongside `statusbar` and `toolbar`) require sufficient LVGL scratch memory during full draw passes.
  - Sizing must be configured to at least `131072` (128 KB) on ESP32-S3 boards and `262144` (256 KB) on ESP32-P4 boards (`CONFIG_LV_MEM_SIZE=131072`).
* **Kernel & Process Dynamic Storage**:
  - Dynamic tables (process tables, file descriptors, pipe tables, cleanup hooks) utilize `kvec_psram_t` with preferred allocation in external PSRAM (`eos_psram_calloc_prefer()`), preserving DMA-capable internal SRAM for high-speed hardware transactions.


