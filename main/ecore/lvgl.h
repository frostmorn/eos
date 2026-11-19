#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
// LVGL Framebuffer & Keyboard Input Support
///////////////////////////////////////////////////////

#include "includes.h"
#include "ecore/input.h"
#include "ecore/ioctl.h"
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_LVGL_DEFAULT_FB_PATH    "/dev/fb0"
#define EOS_LVGL_DEFAULT_KBD_PATH   "/dev/keyboard0"
#define EOS_LVGL_DEFAULT_TOUCH_PATH "/dev/touch0"
#define EOS_LVGL_DMA_STRIP_BYTES    16384U
#define EOS_LVGL_KEY_QUEUE_SIZE     32U
#define EOS_LVGL_TOUCH_QUEUE_SIZE   16U

#define EOS_LVGL_KB_MAX_W          480
#define EOS_LVGL_KB_MIN_H          75
#define EOS_LVGL_KB_MAX_H          240
#define EOS_LVGL_KB_HEIGHT_PCT     50
#define EOS_LVGL_DIALOG_BTN_W      56
#define EOS_LVGL_DIALOG_CANCEL_X   64
#define EOS_LVGL_DIALOG_MIN_KB_H   60
#define EOS_LVGL_DIALOG_MIN_W      200

// Key event callback invoked when a physical key press is polled
typedef void (*eos_lvgl_key_cb_t)(uint16_t eos_key, uint32_t lv_key,
                                  void *user_data);

// Initialization configuration for EOS LVGL support
typedef struct {
  const char *fb_path;    // Framebuffer VFS path (NULL -> $EOS_FB or "/dev/fb0")
  const char *kbd_path;   // Keyboard VFS path (NULL -> $EOS_KEYBOARD or "/dev/keyboard0")
  const char *touch_path; // Touch VFS path (NULL -> $EOS_TOUCH or "/dev/touch0")
  uint32_t partial_lines; // Lines per partial buffer (0 -> auto 16KB strip like st7789)
  bool force_full_frame;  // Start directly in full-frame mode instead of partial mode
  bool dpad_group_nav;    // Map UP/DOWN to LV_KEY_PREV/NEXT for LVGL group navigation
  bool enable_keyboard;   // Open and bind /dev/keyboard0 input device
  bool disable_touch;     // Explicitly disable touch device (enabled by default if device exists)
} eos_lvgl_cfg_t;

// Runtime state of the EOS LVGL framebuffer & input adapter
typedef struct {
  bool initialized;
  int fb_fd;
  int kbd_fd;
  int touch_fd;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t partial_lines;
  size_t partial_buf_size;
  size_t full_buf_size;
  uint16_t *partial_buf;
  uint16_t *full_buf;
  bool fallback_full;
  bool dpad_group_nav;
  uint32_t partial_flushes;
  uint32_t full_flushes;
  lv_display_t *display;
  lv_indev_t *indev;
  lv_indev_t *touch_indev;
  lv_group_t *group;
  eos_lvgl_key_cb_t key_cb;
  void *key_cb_user_data;
} eos_lvgl_ctx_t;

// Initialize LVGL display (partial drawing with full-frame fallback) and keyboard.
// Pass NULL for default configuration (/dev/fb0, /dev/keyboard0, partial mode).
int eos_lvgl_init(const eos_lvgl_cfg_t *cfg);

// Convenience initializer accepting explicit device paths (or NULL for defaults).
int eos_lvgl_init_paths(const char *fb_path, const char *kbd_path);

// Shut down LVGL display/indev/group, free framebuffers, and close device FDs.
void eos_lvgl_deinit(void);

// Access active context and LVGL handles.
eos_lvgl_ctx_t *eos_lvgl_get_ctx(void);
lv_display_t *eos_lvgl_get_display(void);
lv_indev_t *eos_lvgl_get_indev(void);
lv_indev_t *eos_lvgl_get_touch_indev(void);
lv_group_t *eos_lvgl_get_group(void);

// Query active framebuffer dimensions and path.
uint32_t eos_lvgl_get_width(void);
uint32_t eos_lvgl_get_height(void);
const char *eos_lvgl_get_fb_path(void);

// Pump LVGL timers and sleep up to max_sleep_ms milliseconds.
uint32_t eos_lvgl_step(uint32_t max_sleep_ms);

// Control or inspect full-frame fallback state.
void eos_lvgl_set_full_fallback(bool enable);
bool eos_lvgl_is_full_fallback(void);

// Force an immediate full-frame redraw from the shadow framebuffer.
int eos_lvgl_flush_full_frame(void);

// Keyboard input helpers.
void eos_lvgl_set_dpad_group_nav(bool enable);
void eos_lvgl_set_key_cb(eos_lvgl_key_cb_t cb, void *user_data);
bool eos_lvgl_poll_key(uint16_t *eos_key_out);
void eos_lvgl_clear_keys(void);
uint32_t eos_lvgl_map_key(uint16_t eos_key, bool dpad_group_nav);
const char *eos_lvgl_key_name(uint16_t eos_key);

// Touch gesture types
typedef enum {
  EOS_LVGL_GESTURE_NONE = 0,
  EOS_LVGL_GESTURE_TAP,
  EOS_LVGL_GESTURE_SWIPE_UP,
  EOS_LVGL_GESTURE_SWIPE_DOWN,
  EOS_LVGL_GESTURE_SWIPE_LEFT,
  EOS_LVGL_GESTURE_SWIPE_RIGHT,
} eos_lvgl_gesture_t;

// Touch input event structure
typedef struct {
  uint16_t x;
  uint16_t y;
  bool pressed;
  uint16_t pressure; // Touch strength / pressure (0 if unsupported or released)
  eos_lvgl_gesture_t gesture;
} eos_lvgl_touch_event_t;

// Touch event callback
typedef void (*eos_lvgl_touch_cb_t)(const eos_lvgl_touch_event_t *event,
                                    void *user_data);

// Touch input helpers.
void eos_lvgl_set_touch_cb(eos_lvgl_touch_cb_t cb, void *user_data);
bool eos_lvgl_poll_touch(eos_lvgl_touch_event_t *touch_out);
void eos_lvgl_clear_touch(void);
bool eos_lvgl_get_touch_point(uint16_t *x, uint16_t *y, bool *pressed);
bool eos_lvgl_get_touch_point_ex(uint16_t *x, uint16_t *y, uint16_t *pressure,
                                 bool *pressed);
uint16_t eos_lvgl_get_touch_pressure(void);
bool eos_lvgl_has_touch(void);
bool eos_lvgl_has_keyboard(void);

// Bridge touch swipe/tap gestures into synthetic keyboard events (KEY_UP/DOWN/LEFT/RIGHT, ENTER).
void eos_lvgl_set_touch_gestures(bool enable);
bool eos_lvgl_get_touch_gestures(void);

// Touch-friendly standardized 'Back' / 'Exit' button helper for LVGL screens.
// If custom_cb is NULL, clicking the button injects KEY_B to exit the application.
lv_obj_t *eos_lvgl_create_back_btn(lv_obj_t *parent, lv_event_cb_t custom_cb,
                                   void *user_data);

// ===================== EOS Standard Theme Palette =====================
#define EOS_COLOR_BG_DARK        0x0B1120
#define EOS_COLOR_BG_PANEL       0x0F172A
#define EOS_COLOR_BG_CARD        0x1E293B
#define EOS_COLOR_BG_HDR         0x1E3A8A
#define EOS_COLOR_BG_SUBHDR      0x172554
#define EOS_COLOR_BORDER         0x334155
#define EOS_COLOR_BORDER_FOCUS   0x93C5FD
#define EOS_COLOR_TEXT_PRIMARY   0xF8FAFC
#define EOS_COLOR_TEXT_SECONDARY 0xE2E8F0
#define EOS_COLOR_TEXT_MUTED     0x94A3B8
#define EOS_COLOR_TEXT_WHITE     0xFFFFFF
#define EOS_COLOR_ACCENT_BLUE    0x2563EB
#define EOS_COLOR_ACCENT_CYAN    0x38BDF8
#define EOS_COLOR_ACCENT_YELLOW  0xFDE047
#define EOS_COLOR_STATUS_GREEN   0x34D399
#define EOS_COLOR_STATUS_ORANGE  0xFB923C
#define EOS_COLOR_STATUS_RED     0xEF4444

// Semantic Theme Color Aliases
#define EOS_THEME_BG_COLOR         EOS_COLOR_BG_DARK
#define EOS_THEME_PANEL_COLOR      EOS_COLOR_BG_PANEL
#define EOS_THEME_SURFACE_COLOR    EOS_COLOR_BG_CARD
#define EOS_THEME_BORDER_COLOR     EOS_COLOR_BORDER
#define EOS_THEME_TEXT_MUTED       EOS_COLOR_TEXT_MUTED
#define EOS_THEME_TEXT_PRIMARY     EOS_COLOR_TEXT_PRIMARY
#define EOS_THEME_TEXT_WHITE       EOS_COLOR_TEXT_WHITE
#define EOS_THEME_PRIMARY_BLUE     EOS_COLOR_ACCENT_BLUE
#define EOS_THEME_ACCENT_GREEN     EOS_COLOR_STATUS_GREEN
#define EOS_THEME_HIGHLIGHT_YELLOW EOS_COLOR_ACCENT_YELLOW
#define EOS_THEME_INFO_BLUE        EOS_COLOR_ACCENT_CYAN

static inline lv_color_t eos_lvgl_color(uint32_t hex) {
  return lv_color_hex(hex);
}

// ===================== Reusable UI Components =====================

// Creates a standardized styled container card
lv_obj_t *eos_lvgl_create_card(lv_obj_t *parent, int32_t w, int32_t h);

// Creates a standardized top header bar (24px height)
lv_obj_t *eos_lvgl_create_header(lv_obj_t *parent, const char *title, const char *mode_text);

// Creates a standardized toast notification floating at top of parent
lv_obj_t *eos_lvgl_create_toast(lv_obj_t *parent, const char *message, bool is_error);

// Creates a shared on-screen keyboard attached to target_ta (or NULL).
// If parent is NULL, attaches to lv_screen_active().
// Supports touch tapping and hardware keypad navigation (arrows move across keys, Enter/A types key).
// Hardware B and C buttons act as Backspace.
lv_obj_t *eos_lvgl_create_keyboard(lv_obj_t *parent, lv_obj_t *target_ta);

// Returns the currently active shared on-screen keyboard (or NULL if none active)
lv_obj_t *eos_lvgl_get_active_keyboard(void);

// Dispatches a hardware key event (arrows, Enter/A, Backspace/B, Esc) directly to the active on-screen keyboard.
// Returns true if the key was consumed by the keyboard.
bool eos_lvgl_keyboard_handle_key(uint16_t key);

// Callback signature for modal input dialogs
typedef void (*eos_lvgl_input_cb_t)(const char *text, void *user_data);

typedef struct {
  const char *title;         // Dialog title (e.g. "Connect to WiFi")
  const char *prompt;        // Prompt text (e.g. "Enter password:")
  const char *initial_text;  // Initial text field content (or NULL)
  const char *ok_label;      // OK button label (NULL defaults to "OK")
  const char *cancel_label;  // Cancel button label (NULL defaults to "Cancel")
  bool is_password;          // Enable password character masking
  eos_lvgl_input_cb_t ok_cb;     // Invoked on submit
  eos_lvgl_input_cb_t cancel_cb; // Invoked on cancel (or NULL)
  void *user_data;           // Context pointer passed to callbacks
} eos_lvgl_input_dialog_cfg_t;

// Creates a modal text input / password dialog with touch on-screen keyboard and hardware keypad focus
lv_obj_t *eos_lvgl_create_input_dialog(const eos_lvgl_input_dialog_cfg_t *cfg);

// Closes and frees an active input dialog
void eos_lvgl_close_input_dialog(lv_obj_t *dialog);

// ===================== Statusbar & Toolbar APIs =====================

// Sets active toolbar content (/tmp/tbar) bound to current process PID
int eos_toolbar_set(const char *title, const char *shortcuts);
int eos_toolbar_printf(const char *shortcuts, const char *fmt, ...);
int eos_toolbar_clear(void);

// Sets active statusbar content (/tmp/sbar) bound to current process PID
int eos_statusbar_set(const char *line1, const char *line2);
int eos_statusbar_printf(const char *line2, const char *fmt, ...);
int eos_statusbar_clear(void);

// ===================== Shared Modal Dialogs =====================

// Confirmation Dialog
typedef void (*eos_lvgl_confirm_cb_t)(bool confirmed, void *user_data);

typedef struct {
  const char *title;         // Dialog title (e.g. "Confirm Action")
  const char *message;       // Message prompt
  const char *confirm_label; // Confirm button label (NULL defaults to "Confirm")
  const char *cancel_label;  // Cancel button label (NULL defaults to "Cancel")
  bool dangerous;            // If true, confirm button has red accent
  eos_lvgl_confirm_cb_t cb;  // Callback invoked with result
  void *user_data;           // User context pointer
} eos_lvgl_confirm_dialog_cfg_t;

lv_obj_t *eos_lvgl_dialog_confirm(const eos_lvgl_confirm_dialog_cfg_t *cfg);

// Information Dialog
typedef void (*eos_lvgl_info_cb_t)(void *user_data);

typedef struct {
  const char *title;         // Dialog title (e.g. "Information")
  const char *message;       // Message body text
  const char *button_label;  // Dismiss button label (NULL defaults to "OK")
  eos_lvgl_info_cb_t cb;     // Optional callback invoked on dismissal
  void *user_data;           // User context pointer
} eos_lvgl_info_dialog_cfg_t;

lv_obj_t *eos_lvgl_dialog_info(const eos_lvgl_info_dialog_cfg_t *cfg);

// Alert Dialog
typedef enum {
  EOS_LVGL_ALERT_INFO = 0,
  EOS_LVGL_ALERT_WARNING,
  EOS_LVGL_ALERT_ERROR,
} eos_lvgl_alert_level_t;

typedef void (*eos_lvgl_alert_cb_t)(bool accepted, void *user_data);

typedef struct {
  const char *title;            // Dialog title
  const char *message;          // Warning / error message
  eos_lvgl_alert_level_t level; // Severity level (INFO, WARNING, ERROR)
  const char *ok_label;         // Primary button (NULL defaults to "Acknowledge")
  const char *cancel_label;     // Optional secondary button (or NULL for single button)
  eos_lvgl_alert_cb_t cb;       // Callback invoked on user action
  void *user_data;              // User context pointer
} eos_lvgl_alert_dialog_cfg_t;

lv_obj_t *eos_lvgl_dialog_alert(const eos_lvgl_alert_dialog_cfg_t *cfg);

// Closes any shared modal dialog
void eos_lvgl_dialog_close(lv_obj_t *dialog);

#ifdef __cplusplus
}
#endif
