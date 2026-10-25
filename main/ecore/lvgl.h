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

#define EOS_LVGL_DEFAULT_FB_PATH  "/dev/fb0"
#define EOS_LVGL_DEFAULT_KBD_PATH "/dev/keyboard0"

// Key event callback invoked when a physical key press is polled
typedef void (*eos_lvgl_key_cb_t)(uint16_t eos_key, uint32_t lv_key,
                                  void *user_data);

// Initialization configuration for EOS LVGL support
typedef struct {
  const char *fb_path;    // Framebuffer VFS path (NULL -> $EOS_FB or "/dev/fb0")
  const char *kbd_path;   // Keyboard VFS path (NULL -> $EOS_KEYBOARD or "/dev/keyboard0")
  uint32_t partial_lines; // Lines per partial buffer (0 -> auto 16KB strip like st7789)
  bool force_full_frame;  // Start directly in full-frame mode instead of partial mode
  bool dpad_group_nav;    // Map UP/DOWN to LV_KEY_PREV/NEXT for LVGL group navigation
  bool enable_keyboard;   // Open and bind /dev/keyboard0 input device
} eos_lvgl_cfg_t;

// Runtime state of the EOS LVGL framebuffer & input adapter
typedef struct {
  bool initialized;
  int fb_fd;
  int kbd_fd;
  uint32_t width;
  uint32_t height;
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
lv_group_t *eos_lvgl_get_group(void);

// Query active framebuffer dimensions.
uint32_t eos_lvgl_get_width(void);
uint32_t eos_lvgl_get_height(void);

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

#ifdef __cplusplus
}
#endif
