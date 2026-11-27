#include "ecore/lvgl.h"
#include "ecore/proc.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(ESP_PLATFORM) || (defined(__has_include) && __has_include(<esp_heap_caps.h>))
#include <esp_heap_caps.h>
#endif

typedef struct {
  eos_lvgl_ctx_t pub;
  uint16_t indev_key_queue[EOS_LVGL_KEY_QUEUE_SIZE];
  uint8_t indev_key_head;
  uint8_t indev_key_tail;

  uint16_t poll_key_queue[EOS_LVGL_KEY_QUEUE_SIZE];
  uint8_t poll_key_head;
  uint8_t poll_key_tail;

  uint32_t pending_lv_key;
  bool send_release;

  eos_lvgl_touch_event_t touch_queue[EOS_LVGL_TOUCH_QUEUE_SIZE];
  uint8_t touch_head;
  uint8_t touch_tail;
  uint16_t last_touch_x;
  uint16_t last_touch_y;
  uint16_t last_touch_pressure;
  bool touch_pressed;
  uint16_t touch_down_x;
  uint16_t touch_down_y;
  uint32_t touch_down_ms;
  uint16_t touch_max_pressure;
  uint32_t touch_total_travel;
  bool touch_moved;
  bool touch_gestures_enabled;
  char active_fb_path[EOS_LINE_STR_LEN];
  pid_t owner_pid;
  uint32_t fb_offset_x;
  uint32_t fb_offset_y;
  lv_obj_t *active_keyboard;
  eos_lvgl_touch_cb_t touch_cb;
  void *touch_cb_user_data;
} eos_lvgl_internal_t;

static eos_lvgl_internal_t s_lvgl = {
    .pub =
        {
            .initialized = false,
            .fb_fd = -1,
            .kbd_fd = -1,
            .touch_fd = -1,
        },
};

static bool s_lv_core_initialized = false;

// ── Monotonic Millisecond Tick Callback ────────────────────────

static uint32_t eos_lvgl_tick_cb(void) {
  static uint32_t fallback_tick = 0;
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0) {
    uint32_t ms =
        (uint32_t)((uint64_t)tv.tv_sec * (uint64_t)EOS_MS_PER_SEC + (uint64_t)tv.tv_usec / (uint64_t)EOS_US_PER_MS);
    if (ms > fallback_tick) {
      fallback_tick = ms;
    } else {
      fallback_tick++;
    }
    return fallback_tick;
  }
  return ++fallback_tick;
}

// ── Key Queue & Mapping Helpers ────────────────────────────────

static void key_queue_push(uint16_t eos_key) {
  // Push to indev queue (servicing LVGL internal widgets)
  uint8_t indev_next = (uint8_t)((s_lvgl.indev_key_head + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  if (indev_next == s_lvgl.indev_key_tail) {
    s_lvgl.indev_key_tail = (uint8_t)((s_lvgl.indev_key_tail + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  }
  s_lvgl.indev_key_queue[s_lvgl.indev_key_head] = eos_key;
  s_lvgl.indev_key_head = indev_next;

  // Push to poll queue (servicing application event loop eos_lvgl_poll_key)
  uint8_t poll_next = (uint8_t)((s_lvgl.poll_key_head + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  if (poll_next == s_lvgl.poll_key_tail) {
    s_lvgl.poll_key_tail = (uint8_t)((s_lvgl.poll_key_tail + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  }
  s_lvgl.poll_key_queue[s_lvgl.poll_key_head] = eos_key;
  s_lvgl.poll_key_head = poll_next;
}

lv_obj_t *eos_lvgl_get_active_keyboard(void) {
  return s_lvgl.active_keyboard;
}

bool eos_lvgl_keyboard_handle_key(uint16_t key) {
  lv_obj_t *kb = s_lvgl.active_keyboard;
  if (!kb)
    return false;

  switch (key) {
  case KEY_UP:
  case KEY_DOWN:
  case KEY_LEFT:
  case KEY_RIGHT: {
    if (lv_buttonmatrix_get_selected_button(kb) ==
        LV_BUTTONMATRIX_BUTTON_NONE) {
      lv_buttonmatrix_set_selected_button(kb, 0);
    }
    uint32_t lv_k = (key == KEY_RIGHT) ? LV_KEY_RIGHT :
                    (key == KEY_LEFT)  ? LV_KEY_LEFT :
                    (key == KEY_DOWN)  ? LV_KEY_DOWN : LV_KEY_UP;
    lv_obj_send_event(kb, LV_EVENT_KEY, &lv_k);
    return true;
  }
  case KEY_ENTER:
  case KEY_A: {
    uint32_t sel = lv_buttonmatrix_get_selected_button(kb);
    if (sel == LV_BUTTONMATRIX_BUTTON_NONE) {
      sel = 0;
      lv_buttonmatrix_set_selected_button(kb, 0);
    }
    lv_obj_send_event(kb, LV_EVENT_VALUE_CHANGED, &sel);
    return true;
  }
  case KEY_BACKSPACE:
  case KEY_B:
  case KEY_C: {
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    if (ta) {
      lv_textarea_delete_char(ta);
    }
    return true;
  }
  case KEY_ESC: {
    lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
    return true;
  }
  default:
    return false;
  }
}

static void poll_hardware_keyboard(void) {
  if (s_lvgl.pub.kbd_fd < 0)
    return;

  for (int i = 0; i < 8; i++) {
    eos_input_event_t ev;
    memset(&ev, 0, sizeof(ev));
    int ret = ioctl(s_lvgl.pub.kbd_fd, EOS_KEYBOARD_IOCTL_POLL, &ev);
    if (ret < 0) {
      if (errno == ENODEV || errno == EBADF) {
        close(s_lvgl.pub.kbd_fd);
        s_lvgl.pub.kbd_fd = -1;
      }
      break;
    }
    if (ret > 0 && ev.type == EOS_INPUT_EVENT_KEYBOARD &&
        ev.code != KEY_RESERVED) {
      if (s_lvgl.active_keyboard) {
        if (eos_lvgl_keyboard_handle_key(ev.code)) {
          continue;
        }
      }
      key_queue_push(ev.code);
      uint32_t lv_key = eos_lvgl_map_key(ev.code, s_lvgl.pub.dpad_group_nav);
      if (s_lvgl.pub.key_cb) {
        s_lvgl.pub.key_cb(ev.code, lv_key, s_lvgl.pub.key_cb_user_data);
      }
    } else {
      break;
    }
  }
}

uint32_t eos_lvgl_map_key(uint16_t eos_key, bool dpad_group_nav) {
  switch (eos_key) {
  case KEY_UP:
    return dpad_group_nav ? LV_KEY_PREV : LV_KEY_UP;
  case KEY_DOWN:
    return dpad_group_nav ? LV_KEY_NEXT : LV_KEY_DOWN;
  case KEY_LEFT:
    return LV_KEY_LEFT;
  case KEY_RIGHT:
    return LV_KEY_RIGHT;
  case KEY_ENTER:
  case KEY_A:
    return LV_KEY_ENTER;
  case KEY_ESC:
    return LV_KEY_ESC;
  case KEY_BACKSPACE:
  case KEY_B:
    return LV_KEY_BACKSPACE;
  case KEY_C:
    if (s_lvgl.pub.group && lv_group_get_editing(s_lvgl.pub.group)) {
      return LV_KEY_BACKSPACE;
    }
    return LV_KEY_PREV;
  case KEY_D:
    return LV_KEY_NEXT;
  case KEY_SPACE:
    return ' ';
  case KEY_TAB:
    return LV_KEY_NEXT;
  case KEY_HOME:
    return LV_KEY_HOME;
  case KEY_END:
    return LV_KEY_END;
  case KEY_DELETE:
    return LV_KEY_DEL;
  default:
    return 0;
  }
}

const char *eos_lvgl_key_name(uint16_t eos_key) {
  switch (eos_key) {
  case KEY_UP:
    return "UP";
  case KEY_DOWN:
    return "DOWN";
  case KEY_LEFT:
    return "LEFT";
  case KEY_RIGHT:
    return "RIGHT";
  case KEY_ENTER:
    return "SELECT/ENTER";
  case KEY_SPACE:
    return "START/SPACE";
  case KEY_A:
    return "A";
  case KEY_B:
    return "B";
  case KEY_C:
    return "C";
  case KEY_D:
    return "D";
  case KEY_ESC:
    return "ESC";
  case KEY_SLEEP:
    return "SLEEP";
  default:
    return "KEY";
  }
}

static void eos_lvgl_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (!data)
    return;

  // Complete the previous key press cycle with a release event so LVGL
  // dispatches LV_EVENT_RELEASED / LV_EVENT_CLICKED properly.
  if (s_lvgl.send_release) {
    data->key = s_lvgl.pending_lv_key;
    data->state = LV_INDEV_STATE_RELEASED;
    s_lvgl.send_release = false;
    data->continue_reading = (s_lvgl.indev_key_head != s_lvgl.indev_key_tail);
    return;
  }

  // Poll hardware events into queue
  poll_hardware_keyboard();

  if (s_lvgl.indev_key_head != s_lvgl.indev_key_tail) {
    uint16_t code = s_lvgl.indev_key_queue[s_lvgl.indev_key_tail];
    s_lvgl.indev_key_tail = (uint8_t)((s_lvgl.indev_key_tail + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);

    bool group_nav = s_lvgl.pub.dpad_group_nav;
    if (group_nav && s_lvgl.pub.group &&
        lv_group_get_editing(s_lvgl.pub.group)) {
      group_nav = false;
    }
    uint32_t lv_key = eos_lvgl_map_key(code, group_nav);
    if (lv_key != 0) {
      s_lvgl.pending_lv_key = lv_key;
      s_lvgl.send_release = true;
      data->key = lv_key;
      data->state = LV_INDEV_STATE_PRESSED;
      data->continue_reading = true;
      return;
    }
  }

  data->state = LV_INDEV_STATE_RELEASED;
}

static void touch_queue_push(const eos_lvgl_touch_event_t *ev) {
  uint8_t next = (uint8_t)((s_lvgl.touch_head + 1U) % EOS_LVGL_TOUCH_QUEUE_SIZE);
  if (next == s_lvgl.touch_tail) {
    s_lvgl.touch_tail =
        (uint8_t)((s_lvgl.touch_tail + 1U) % EOS_LVGL_TOUCH_QUEUE_SIZE);
  }
  s_lvgl.touch_queue[s_lvgl.touch_head] = *ev;
  s_lvgl.touch_head = next;
}

static void poll_hardware_touch(void) {
  if (s_lvgl.pub.touch_fd < 0)
    return;

  eos_input_event_t ev;
  memset(&ev, 0, sizeof(ev));
  int ret = ioctl(s_lvgl.pub.touch_fd, EOS_TOUCH_IOCTL_POLL, &ev);
  if (ret < 0) {
    if (errno == ENODEV || errno == EBADF) {
      close(s_lvgl.pub.touch_fd);
      s_lvgl.pub.touch_fd = -1;
    }
    return;
  }
  if (ret > 0 && ev.type == EOS_INPUT_EVENT_TOUCH) {
    int32_t mx = (int32_t)ev.x - (int32_t)s_lvgl.fb_offset_x;
    int32_t my = (int32_t)ev.y - (int32_t)s_lvgl.fb_offset_y;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (s_lvgl.pub.width > 0 && mx >= (int32_t)s_lvgl.pub.width) {
      mx = (int32_t)s_lvgl.pub.width - 1;
    }
    if (s_lvgl.pub.height > 0 && my >= (int32_t)s_lvgl.pub.height) {
      my = (int32_t)s_lvgl.pub.height - 1;
    }

    s_lvgl.last_touch_x = (uint16_t)mx;
    s_lvgl.last_touch_y = (uint16_t)my;
    s_lvgl.last_touch_pressure = (ev.state != 0) ? ev.pressure : 0;
    s_lvgl.touch_pressed = (ev.state != 0);

    eos_lvgl_touch_event_t tev = {
        .x = (uint16_t)mx,
        .y = (uint16_t)my,
        .pressed = s_lvgl.touch_pressed,
        .pressure = s_lvgl.last_touch_pressure,
        .gesture = EOS_LVGL_GESTURE_NONE,
    };
    touch_queue_push(&tev);

    if (s_lvgl.touch_cb) {
      s_lvgl.touch_cb(&tev, s_lvgl.touch_cb_user_data);
    }
  }
}

static void eos_lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  poll_hardware_touch();

  data->point.x = s_lvgl.last_touch_x;
  data->point.y = s_lvgl.last_touch_y;
  data->state = s_lvgl.touch_pressed ? LV_INDEV_STATE_PRESSED
                                     : LV_INDEV_STATE_RELEASED;
}

// ── Framebuffer Partial & Full-Frame Flushing ──────────────────

// Round invalidated areas horizontally to full display width so LVGL's
// draw buffer row stride always matches ctx->width * 2 without stride-alignment
// padding shifts.
static void eos_lvgl_invalidate_area_cb(lv_event_t *e) {
  eos_lvgl_ctx_t *ctx = (eos_lvgl_ctx_t *)lv_event_get_user_data(e);
  lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
  if (!ctx || !area || ctx->width == 0 || ctx->height == 0)
    return;

  if (area->y1 < 0)
    area->y1 = 0;
  if (area->y2 >= (int32_t)ctx->height)
    area->y2 = (int32_t)ctx->height - 1;
  if (area->y1 > area->y2)
    area->y2 = area->y1;

  area->x1 = 0;
  area->x2 = (int32_t)ctx->width - 1;
}

// Writes a full frame to /dev/fb0 using windowed row strips (matching the
// partial draw coordinate mapping), falling back to a single unclipped write
// only if SET_WINDOW is unsupported.
static int eos_lvgl_write_full_frame_buf(eos_lvgl_ctx_t *ctx,
                                         const uint16_t *frame_pixels) {
  if (!ctx || ctx->fb_fd < 0 || !frame_pixels)
    return -1;

  size_t bytes_per_pixel = eos_display_format_bytes_per_pixel(ctx->format);
  size_t row_bytes = (size_t)ctx->width * bytes_per_pixel;
  uint32_t strip_h =
      (ctx->partial_lines > 0)
          ? ctx->partial_lines
          : (uint32_t)(EOS_LVGL_DMA_STRIP_BYTES / row_bytes);
  if (strip_h == 0)
    strip_h = 1;
  if (strip_h > ctx->height)
    strip_h = ctx->height;

  bool strip_ok = true;
  for (uint32_t sy = 0; sy < ctx->height; sy += strip_h) {
    uint32_t sh = (sy + strip_h <= ctx->height) ? strip_h : (ctx->height - sy);
    eos_display_rect_t rect = {
        .x = 0,
        .y = sy,
        .w = ctx->width,
        .h = sh,
    };
    if (ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect) != 0) {
      strip_ok = false;
      break;
    }
    size_t chunk_bytes = (size_t)sh * row_bytes;
    const uint8_t *src = (const uint8_t *)frame_pixels + (size_t)sy * row_bytes;
    ssize_t rc = write(ctx->fb_fd, src, chunk_bytes);
    if (rc < 0 || (rc > 0 && (size_t)rc != chunk_bytes)) {
      strip_ok = false;
      break;
    }
  }

  if (strip_ok) {
    ctx->full_flushes++;
    return 0;
  }

  // Unclipped single-write fallback if SET_WINDOW is not supported
  (void)ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, NULL);
  size_t full_bytes = (size_t)ctx->width * ctx->height * bytes_per_pixel;
  ssize_t written = write(ctx->fb_fd, frame_pixels, full_bytes);
  if (written >= 0 && (written == 0 || (size_t)written == full_bytes)) {
    ctx->full_flushes++;
    return 0;
  }

  return -1;
}

static void eos_lvgl_blit_to_shadow(eos_lvgl_ctx_t *ctx, const lv_area_t *area,
                                    const uint16_t *src) {
  if (!ctx || !ctx->full_buf || !area || !src)
    return;
  if ((const void *)src == (const void *)ctx->full_buf)
    return;

  int32_t x1 = area->x1;
  int32_t y1 = area->y1;
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  if (w <= 0 || h <= 0)
    return;

  // Respect LVGL's draw buffer stride alignment (LV_DRAW_BUF_STRIDE_ALIGN)
  lv_color_format_t lv_cf = (ctx->format == EOS_DISPLAY_FMT_RGB888) ? LV_COLOR_FORMAT_RGB888 : LV_COLOR_FORMAT_RGB565;
  size_t bpp = eos_display_format_bytes_per_pixel(ctx->format);
  uint32_t src_stride_bytes = lv_draw_buf_width_to_stride((uint32_t)w, lv_cf);
  if (src_stride_bytes < (uint32_t)w * bpp) {
    src_stride_bytes = (uint32_t)w * bpp;
  }

  const uint8_t *src_bytes = (const uint8_t *)src;
  uint8_t *dst_base = (uint8_t *)ctx->full_buf;

  for (int32_t row = 0; row < h; row++) {
    int32_t dst_y = y1 + row;
    if (dst_y < 0 || dst_y >= (int32_t)ctx->height)
      continue;

    int32_t dst_x = (x1 < 0) ? 0 : x1;
    int32_t src_x = (x1 < 0) ? -x1 : 0;
    int32_t copy_w = w - src_x;
    if (dst_x + copy_w > (int32_t)ctx->width) {
      copy_w = (int32_t)ctx->width - dst_x;
    }
    if (copy_w <= 0)
      continue;

    const uint8_t *src_row = src_bytes + (size_t)row * src_stride_bytes;
    memcpy(dst_base + ((size_t)dst_y * ctx->width + (size_t)dst_x) * bpp,
           src_row + (size_t)src_x * bpp, (size_t)copy_w * bpp);
  }
}

static void eos_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area,
                              uint8_t *px_map) {
  eos_lvgl_ctx_t *ctx = &s_lvgl.pub;
  if (!ctx->initialized || ctx->fb_fd < 0 || !area || !px_map) {
    lv_display_flush_ready(display);
    return;
  }

  int32_t x1 = area->x1;
  int32_t y1 = area->y1;
  int32_t x2 = area->x2;
  int32_t y2 = area->y2;
  int32_t w = x2 - x1 + 1;
  int32_t h = y2 - y1 + 1;

  // Keep shadow full-frame buffer synchronized so fallback can recover at any point
  eos_lvgl_blit_to_shadow(ctx, area, (const uint16_t *)px_map);

  if (!ctx->fallback_full) {
    bool valid_rect = (x1 >= 0 && y1 >= 0 && w > 0 && h > 0 &&
                       (uint32_t)(x1 + w) <= ctx->width &&
                       (uint32_t)(y1 + h) <= ctx->height);

    if (valid_rect) {
      eos_display_rect_t rect = {
          .x = (uint32_t)x1,
          .y = (uint32_t)y1,
          .w = (uint32_t)w,
          .h = (uint32_t)h,
      };

      if (ioctl(ctx->fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &rect) == 0) {
        size_t bpp = eos_display_format_bytes_per_pixel(ctx->format);
        size_t chunk_bytes = (size_t)w * (size_t)h * bpp;
        ssize_t written = write(ctx->fb_fd, px_map, chunk_bytes);
        if (written >= 0 && (written == 0 || (size_t)written == chunk_bytes)) {
          ctx->partial_flushes++;
          lv_display_flush_ready(display);
          return;
        }
      }
    }

    // Partial drawing failed — switch to full-frame fallback
    EOS_LOGW("LVGL partial draw failed (%ld,%ld %ldx%ld); falling back to full "
             "frame drawing",
             (long)x1, (long)y1, (long)w, (long)h);
    ctx->fallback_full = true;
  }

  // Full-frame fallback path: flush complete shadow frame once LVGL finishes
  // rendering the last dirty strip of the current refresh cycle.
  if (lv_display_flush_is_last(display)) {
    const uint16_t *full_src =
        ctx->full_buf ? ctx->full_buf : (const uint16_t *)px_map;
    if (eos_lvgl_write_full_frame_buf(ctx, full_src) < 0) {
      EOS_LOGE("LVGL full-frame fallback write failed");
    }
  }

  lv_display_flush_ready(display);
}

// ── Public Lifecycle & Helper Functions ────────────────────────

static void lvgl_proc_cleanup_hook(void *arg) {
  (void)arg;
  if (s_lvgl.pub.initialized) {
    eos_lvgl_deinit();
  }
}

static int lvgl_init_probe_fb(const char *fb_path, uint32_t *out_w, uint32_t *out_h) {
  int fb_fd = open(fb_path, O_RDWR);
  if (fb_fd < 0) {
    EOS_LOGE("Failed to open framebuffer '%s': %s", fb_path, strerror(errno));
    return -1;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &width) != 0 ||
      ioctl(fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &height) != 0 || width == 0 ||
      height == 0) {
    EOS_LOGE("Failed to query display dimensions on '%s'", fb_path);
    close(fb_fd);
    return -1;
  }

  s_lvgl.pub.fb_fd = fb_fd;
  s_lvgl.pub.width = width;
  s_lvgl.pub.height = height;

  uint32_t fmt = EOS_DISPLAY_FMT_RGB565;
  if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_GET_FORMAT, &fmt) == 0) {
    s_lvgl.pub.format = fmt;
  } else {
    s_lvgl.pub.format = EOS_DISPLAY_FMT_RGB565;
  }

  uint32_t off_x = 0, off_y = 0;
  if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_GET_OFFSET_X, &off_x) == 0) {
    s_lvgl.fb_offset_x = off_x;
  }
  if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_GET_OFFSET_Y, &off_y) == 0) {
    s_lvgl.fb_offset_y = off_y;
  }

  *out_w = width;
  *out_h = height;
  return fb_fd;
}

static int lvgl_init_buffers(const eos_lvgl_cfg_t *cfg, uint32_t width,
                             uint32_t height, int fb_fd) {
  size_t bytes_per_pixel = eos_display_format_bytes_per_pixel(s_lvgl.pub.format);
  size_t row_bytes = (size_t)width * bytes_per_pixel;
  uint32_t strip_h =
      (cfg && cfg->partial_lines > 0)
          ? cfg->partial_lines
          : (uint32_t)(EOS_LVGL_DMA_STRIP_BYTES / row_bytes);
  if (strip_h == 0)
    strip_h = 1;
  if (strip_h > height)
    strip_h = height;

  s_lvgl.pub.partial_lines = strip_h;
  s_lvgl.pub.partial_buf_size = (size_t)strip_h * row_bytes;
  s_lvgl.pub.full_buf_size = (size_t)width * (size_t)height * bytes_per_pixel;

#ifdef MALLOC_CAP_SPIRAM
  s_lvgl.pub.full_buf = heap_caps_malloc(
      s_lvgl.pub.full_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  if (!s_lvgl.pub.full_buf) {
    s_lvgl.pub.full_buf = malloc(s_lvgl.pub.full_buf_size);
  }
  if (s_lvgl.pub.full_buf) {
    memset(s_lvgl.pub.full_buf, 0, s_lvgl.pub.full_buf_size);
  }

  bool force_full = cfg ? cfg->force_full_frame : false;
  if (!force_full) {
#ifdef MALLOC_CAP_SPIRAM
    s_lvgl.pub.partial_buf = heap_caps_malloc(
        s_lvgl.pub.partial_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!s_lvgl.pub.partial_buf) {
      s_lvgl.pub.partial_buf = malloc(s_lvgl.pub.partial_buf_size);
    }
    if (s_lvgl.pub.partial_buf) {
      memset(s_lvgl.pub.partial_buf, 0, s_lvgl.pub.partial_buf_size);
    }
  }

  eos_display_rect_t probe_rect = {
      .x = 0, .y = 0, .w = width, .h = height};
  if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &probe_rect) != 0) {
    EOS_LOGW("Display driver rejected SET_WINDOW; enabling full-frame fallback");
    s_lvgl.pub.fallback_full = true;
  } else {
    (void)ioctl(fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, NULL);
  }

  if (!s_lvgl.pub.partial_buf) {
    if (!s_lvgl.pub.full_buf) {
      EOS_LOGE("Failed to allocate both partial and full LVGL framebuffers");
      close(fb_fd);
      s_lvgl.pub.fb_fd = -1;
      return -1;
    }
    EOS_LOGW("Using full-frame buffer directly (partial buffer unavailable)");
    s_lvgl.pub.fallback_full = true;
  }

  return 0;
}

static void lvgl_init_inputs(bool enable_kbd, const char *kbd_path,
                             bool enable_touch, const char *touch_path,
                             lv_display_t *disp) {
  if (enable_kbd) {
    int kbd_fd = open(kbd_path, O_RDONLY);
    if (kbd_fd >= 0) {
      s_lvgl.pub.kbd_fd = kbd_fd;
      lv_indev_t *indev = lv_indev_create();
      if (indev) {
        lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(indev, eos_lvgl_indev_read_cb);
        if (s_lvgl.pub.group) {
          lv_indev_set_group(indev, s_lvgl.pub.group);
        }
        s_lvgl.pub.indev = indev;
      }
    } else {
      EOS_LOGW("Keyboard device '%s' not available: %s", kbd_path,
               strerror(errno));
    }
  }

  if (enable_touch) {
    int touch_fd = open(touch_path, O_RDWR);
    if (touch_fd < 0) {
      touch_fd = open(touch_path, O_RDONLY);
    }
    if (touch_fd < 0 && strcmp(touch_path, "/dev/touch0") == 0) {
      touch_fd = open("/dev/touch", O_RDONLY);
    }
    if (touch_fd >= 0) {
      s_lvgl.pub.touch_fd = touch_fd;
      lv_indev_t *tindev = lv_indev_create();
      if (tindev) {
        lv_indev_set_type(tindev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(tindev, eos_lvgl_touch_read_cb);
        lv_indev_set_user_data(tindev, &s_lvgl.pub);
        lv_indev_set_display(tindev, disp);
        s_lvgl.pub.touch_indev = tindev;
      }
    } else {
      EOS_LOGI("Touch device '%s' not available: %s", touch_path,
               strerror(errno));
    }
  }
}

// ── Emoji / Symbol Font Fallback Layer ─────────────────────────

extern const lv_font_t lv_font_montserrat_14;

typedef struct {
  uint32_t emoji;
  uint16_t symbol;
} eos_emoji_map_t;

static const eos_emoji_map_t s_emoji_symbols[] = {
  {0x1F4C1, 0xF07B}, // 📁 -> LV_SYMBOL_DIRECTORY
  {0x1F4C2, 0xF07B}, // 📂 -> LV_SYMBOL_DIRECTORY
  {0x1F4C4, 0xF15B}, // 📄 -> LV_SYMBOL_FILE
  {0x1F4CB, 0xF15B}, // 📋 -> LV_SYMBOL_FILE
  {0x1F4DD, 0xF0C4}, // 📝 -> LV_SYMBOL_EDIT
  {0x25B6,  0xF04B}, // ▶ / ▶️ -> LV_SYMBOL_PLAY
  {0x23F8,  0xF04C}, // ⏸ -> LV_SYMBOL_PAUSE
  {0x23F9,  0xF04D}, // ⏹ -> LV_SYMBOL_STOP
  {0x25B2,  0xF077}, // ▲ -> LV_SYMBOL_UP
  {0x25BC,  0xF078}, // ▼ -> LV_SYMBOL_DOWN
  {0x25C4,  0xF053}, // ◄ -> LV_SYMBOL_LEFT
  {0x25BA,  0xF054}, // ► -> LV_SYMBOL_RIGHT
  {0x2B06,  0xF077}, // ⬆ / ⬆️ -> LV_SYMBOL_UP
  {0x2B07,  0xF078}, // ⬇ / ⬇️ -> LV_SYMBOL_DOWN
  {0x27A1,  0xF054}, // ➡ -> LV_SYMBOL_RIGHT
  {0x2B05,  0xF053}, // ⬅ -> LV_SYMBOL_LEFT
  {0x1F5BC, 0xF03E}, // 🖼 / 🖼️ -> LV_SYMBOL_IMAGE
  {0x1F304, 0xF03E}, // 🌄 -> LV_SYMBOL_IMAGE
  {0x1F3B5, 0xF001}, // 🎵 -> LV_SYMBOL_AUDIO
  {0x1F3A4, 0xF001}, // 🎤 -> LV_SYMBOL_AUDIO
  {0x1F5D1, 0xF2ED}, // 🗑 / 🗑️ -> LV_SYMBOL_TRASH
  {0x1F441, 0xF06E}, // 👁 / 👁️ -> LV_SYMBOL_EYE_OPEN
  {0x274C,  0xF00D}, // ❌ -> LV_SYMBOL_CLOSE
  {0x2795,  0xF067}, // ➕ -> LV_SYMBOL_PLUS
  {0x2796,  0xF068}, // ➖ -> LV_SYMBOL_MINUS
  {0x1F504, 0xF021}, // 🔄 -> LV_SYMBOL_REFRESH
  {0x1F4BE, 0xF7C2}, // 💾 -> LV_SYMBOL_SD_CARD
  {0x1F3E0, 0xF015}, // 🏠 -> LV_SYMBOL_HOME
  {0x2328,  0xF11C}, // ⌨ -> LV_SYMBOL_KEYBOARD
  {0x2699,  0xF013}, // ⚙ -> LV_SYMBOL_SETTINGS
  {0x1F4F6, 0xF1EB}, // 📶 -> LV_SYMBOL_WIFI
  {0x1F50B, 0xF240}, // 🔋 -> LV_SYMBOL_BATTERY_FULL
  {0x26A1,  0xF0E7}, // ⚡ -> LV_SYMBOL_CHARGE
  {0x1F514, 0xF0F3}, // 🔔 -> LV_SYMBOL_BELL
  {0x1F50D, 0xF002}, // 🔍 -> LV_SYMBOL_SEARCH
  {0x2705,  0xF00C}, // ✅ -> LV_SYMBOL_OK
  {0x26A0,  0xF071}, // ⚠️ -> LV_SYMBOL_WARNING
};

static uint32_t eos_map_emoji_to_symbol(uint32_t cp) {
  for (size_t i = 0; i < sizeof(s_emoji_symbols) / sizeof(s_emoji_symbols[0]); i++) {
    if (s_emoji_symbols[i].emoji == cp) {
      return s_emoji_symbols[i].symbol;
    }
  }
  return 0;
}

static bool eos_emoji_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                    uint32_t letter, uint32_t letter_next) {
  (void)font;
  if (!dsc_out) return false;

  // Ignore unicode variation selector 16 (VS16) transparently
  if (letter == 0xFE0F) {
    memset(dsc_out, 0, sizeof(*dsc_out));
    dsc_out->format = LV_FONT_GLYPH_FORMAT_NONE;
    return true;
  }

  uint32_t sym = eos_map_emoji_to_symbol(letter);
  if (!sym) return false;

  const lv_font_t *base = &lv_font_montserrat_14;
  return base->get_glyph_dsc(base, dsc_out, sym, letter_next);
}

static const void *eos_emoji_get_glyph_bitmap(lv_font_glyph_dsc_t *g_dsc,
                                              lv_draw_buf_t *draw_buf) {
  if (!g_dsc) return NULL;
  const lv_font_t *base = &lv_font_montserrat_14;
  return base->get_glyph_bitmap(g_dsc, draw_buf);
}

static lv_font_t s_eos_emoji_font;
static lv_font_t s_eos_main_font;
static bool s_eos_fonts_inited = false;

static void eos_init_fonts(void) {
  if (s_eos_fonts_inited) return;

  s_eos_emoji_font = lv_font_montserrat_14;
  s_eos_emoji_font.get_glyph_dsc = eos_emoji_get_glyph_dsc;
  s_eos_emoji_font.get_glyph_bitmap = eos_emoji_get_glyph_bitmap;
  s_eos_emoji_font.fallback = NULL;

  s_eos_main_font = lv_font_montserrat_14;
  s_eos_main_font.fallback = &s_eos_emoji_font;

  s_eos_fonts_inited = true;
}

const lv_font_t *eos_lvgl_get_font(void) {
  eos_init_fonts();
  return &s_eos_main_font;
}

static void lvgl_init_core(void) {
  if (s_lv_core_initialized) {
    return;
  }
  lv_init();
  lv_tick_set_cb(eos_lvgl_tick_cb);
#if CONFIG_SPIRAM
  void *psram_pool = heap_caps_malloc(EOS_LVGL_PSRAM_POOL_SIZE, MALLOC_CAP_SPIRAM);
  if (psram_pool) {
    if (lv_mem_add_pool(psram_pool, EOS_LVGL_PSRAM_POOL_SIZE)) {
      EOS_LOGI("LVGL: expanded memory with PSRAM pool (%u bytes)", (unsigned)EOS_LVGL_PSRAM_POOL_SIZE);
    }
  }
#endif
  s_lv_core_initialized = true;
}

int eos_lvgl_init(const eos_lvgl_cfg_t *cfg) {
  pid_t cur_pid = eos_getpid();
  if (s_lvgl.pub.initialized) {
    if (s_lvgl.owner_pid > 0 && (!eos_proc_is_alive(s_lvgl.owner_pid) || s_lvgl.owner_pid == cur_pid)) {
      EOS_LOGW("eos_lvgl_init: reclaiming LVGL from PID %d", s_lvgl.owner_pid);
      eos_lvgl_deinit();
    } else {
      EOS_LOGW("eos_lvgl_init: LVGL is already initialized by active process %d", s_lvgl.owner_pid);
      return -EBUSY;
    }
  }

  memset(&s_lvgl, 0, sizeof(s_lvgl));
  s_lvgl.owner_pid = cur_pid;
  eos_proc_on_exit(lvgl_proc_cleanup_hook, NULL, "lvgl_deinit");
  s_lvgl.pub.fb_fd = -1;
  s_lvgl.pub.kbd_fd = -1;
  s_lvgl.pub.touch_fd = -1;
  s_lvgl.touch_gestures_enabled = false;

  const char *fb_path = (cfg && cfg->fb_path) ? cfg->fb_path : getenv("EOS_FB");
  if (!fb_path || !fb_path[0]) {
    fb_path = (access("/dev/vfb0", F_OK) == 0) ? "/dev/vfb0" : EOS_LVGL_DEFAULT_FB_PATH;
  }
  strlcpy(s_lvgl.active_fb_path, fb_path, sizeof(s_lvgl.active_fb_path));

  const char *kbd_path =
      (cfg && cfg->kbd_path) ? cfg->kbd_path : getenv("EOS_KEYBOARD");
  if (!kbd_path || !kbd_path[0]) {
    kbd_path = EOS_LVGL_DEFAULT_KBD_PATH;
  }

  const char *touch_path =
      (cfg && cfg->touch_path) ? cfg->touch_path : getenv("EOS_TOUCH");
  if (!touch_path || !touch_path[0]) {
    touch_path = EOS_LVGL_DEFAULT_TOUCH_PATH;
  }

  uint32_t width = 0, height = 0;
  int fb_fd = lvgl_init_probe_fb(fb_path, &width, &height);
  if (fb_fd < 0) {
    return -1;
  }

  s_lvgl.pub.dpad_group_nav = cfg ? cfg->dpad_group_nav : true;

  if (lvgl_init_buffers(cfg, width, height, fb_fd) != 0) {
    return -1;
  }

  lvgl_init_core();

  lv_display_t *disp = lv_display_create((int32_t)width, (int32_t)height);
  if (!disp) {
    EOS_LOGE("Failed to create LVGL display");
    free(s_lvgl.pub.partial_buf);
    free(s_lvgl.pub.full_buf);
    close(fb_fd);
    s_lvgl.pub.partial_buf = NULL;
    s_lvgl.pub.full_buf = NULL;
    s_lvgl.pub.fb_fd = -1;
    return -1;
  }

  lv_color_format_t lv_cf = (s_lvgl.pub.format == EOS_DISPLAY_FMT_RGB888) ? LV_COLOR_FORMAT_RGB888 : LV_COLOR_FORMAT_RGB565;
  lv_display_set_color_format(disp, lv_cf);

  if (s_lvgl.pub.partial_buf) {
    lv_display_set_buffers(disp, s_lvgl.pub.partial_buf, NULL,
                           s_lvgl.pub.partial_buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
  } else {
    lv_display_set_buffers(disp, s_lvgl.pub.full_buf, NULL,
                           s_lvgl.pub.full_buf_size,
                           LV_DISPLAY_RENDER_MODE_FULL);
  }

  lv_display_add_event_cb(disp, eos_lvgl_invalidate_area_cb,
                          LV_EVENT_INVALIDATE_AREA, &s_lvgl.pub);
  lv_display_set_flush_cb(disp, eos_lvgl_flush_cb);
  s_lvgl.pub.display = disp;

  eos_init_fonts();
  lv_theme_t *th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE),
                                         lv_palette_main(LV_PALETTE_RED),
                                         LV_THEME_DEFAULT_DARK, &s_eos_main_font);
  lv_display_set_theme(disp, th);
  lv_obj_t *act_scr = lv_display_get_screen_active(disp);
  if (act_scr) {
    lv_obj_set_style_text_font(act_scr, &s_eos_main_font, 0);
  }

  s_lvgl.pub.group = lv_group_create();
  if (s_lvgl.pub.group) {
    lv_group_set_default(s_lvgl.pub.group);
  }

  bool enable_kbd = cfg ? cfg->enable_keyboard : true;
  bool enable_touch = cfg ? (!cfg->disable_touch) : true;
  lvgl_init_inputs(enable_kbd, kbd_path, enable_touch, touch_path, disp);

  s_lvgl.pub.initialized = true;
  return 0;
}

int eos_lvgl_init_paths(const char *fb_path, const char *kbd_path) {
  eos_lvgl_cfg_t cfg = {
      .fb_path = fb_path,
      .kbd_path = kbd_path,
      .partial_lines = 0,
      .force_full_frame = false,
      .dpad_group_nav = true,
      .enable_keyboard = true,
  };
  return eos_lvgl_init(&cfg);
}

const char *eos_lvgl_get_fb_path(void) {
  return s_lvgl.active_fb_path[0] ? s_lvgl.active_fb_path : EOS_LVGL_DEFAULT_FB_PATH;
}

void eos_lvgl_deinit(void) {
  if (!s_lvgl.pub.initialized)
    return;

  if (s_lvgl.pub.display) {
    lv_obj_t *scr = lv_screen_active();
    if (scr) {
      lv_obj_clean(scr);
    }
  }

  if (s_lvgl.pub.touch_indev) {
    lv_indev_delete(s_lvgl.pub.touch_indev);
    s_lvgl.pub.touch_indev = NULL;
  }

  if (s_lvgl.pub.indev) {
    lv_indev_delete(s_lvgl.pub.indev);
    s_lvgl.pub.indev = NULL;
  }

  if (s_lvgl.pub.group) {
    lv_group_delete(s_lvgl.pub.group);
    s_lvgl.pub.group = NULL;
  }

  if (s_lvgl.pub.display) {
    lv_display_delete(s_lvgl.pub.display);
    s_lvgl.pub.display = NULL;
  }

  if (s_lvgl.pub.partial_buf) {
    free(s_lvgl.pub.partial_buf);
    s_lvgl.pub.partial_buf = NULL;
  }

  if (s_lvgl.pub.full_buf) {
    free(s_lvgl.pub.full_buf);
    s_lvgl.pub.full_buf = NULL;
  }

  if (s_lvgl.pub.fb_fd >= 0) {
    close(s_lvgl.pub.fb_fd);
    s_lvgl.pub.fb_fd = -1;
  }

  if (s_lvgl.pub.kbd_fd >= 0) {
    close(s_lvgl.pub.kbd_fd);
    s_lvgl.pub.kbd_fd = -1;
  }

  if (s_lvgl.pub.touch_fd >= 0) {
    close(s_lvgl.pub.touch_fd);
    s_lvgl.pub.touch_fd = -1;
  }

  if (s_lvgl.owner_pid > 0) {
    eos_proc_cancel_exit_match(s_lvgl.owner_pid, lvgl_proc_cleanup_hook, NULL);
    s_lvgl.owner_pid = 0;
  }

  eos_lvgl_clear_keys();
  s_lvgl.active_keyboard = NULL;
  s_lvgl.pub.initialized = false;
}

eos_lvgl_ctx_t *eos_lvgl_get_ctx(void) {
  return s_lvgl.pub.initialized ? &s_lvgl.pub : NULL;
}

lv_display_t *eos_lvgl_get_display(void) { return s_lvgl.pub.display; }

lv_indev_t *eos_lvgl_get_indev(void) { return s_lvgl.pub.indev; }

lv_indev_t *eos_lvgl_get_touch_indev(void) { return s_lvgl.pub.touch_indev; }

lv_group_t *eos_lvgl_get_group(void) { return s_lvgl.pub.group; }

uint32_t eos_lvgl_get_width(void) { return s_lvgl.pub.width; }

uint32_t eos_lvgl_get_height(void) { return s_lvgl.pub.height; }

eos_screen_size_t eos_screen_size_from_dim(uint32_t width, uint32_t height) {
  if (width <= EOS_SCREEN_COMPACT_WIDTH_MAX || height <= EOS_SCREEN_COMPACT_HEIGHT_MAX) {
    return EOS_SCREEN_SIZE_COMPACT;
  }
  if (height >= EOS_SCREEN_LARGE_HEIGHT_MIN) {
    return EOS_SCREEN_SIZE_LARGE;
  }
  return EOS_SCREEN_SIZE_REGULAR;
}

bool eos_screen_is_compact_dim(uint32_t width, uint32_t height) {
  return (width <= EOS_SCREEN_COMPACT_WIDTH_MAX || height <= EOS_SCREEN_COMPACT_HEIGHT_MAX);
}

bool eos_screen_is_large_dim(uint32_t width, uint32_t height) {
  return (width > EOS_SCREEN_COMPACT_WIDTH_MAX && height >= EOS_SCREEN_LARGE_HEIGHT_MIN);
}

eos_screen_size_t eos_screen_get_size(void) {
  return eos_screen_size_from_dim(s_lvgl.pub.width, s_lvgl.pub.height);
}

bool eos_screen_is_compact(void) {
  return eos_screen_is_compact_dim(s_lvgl.pub.width, s_lvgl.pub.height);
}

bool eos_screen_is_large(void) {
  return eos_screen_is_large_dim(s_lvgl.pub.width, s_lvgl.pub.height);
}

uint32_t eos_lvgl_step(uint32_t max_sleep_ms) {
  if (!s_lvgl.pub.initialized)
    return 0;

  uint32_t delay = lv_timer_handler();
  if (max_sleep_ms == 0)
    max_sleep_ms = 10;
  if (delay > max_sleep_ms)
    delay = max_sleep_ms;
  if (delay == 0)
    delay = 1;

  usleep(delay * (uint32_t)EOS_US_PER_MS);
  return delay;
}

void eos_lvgl_set_full_fallback(bool enable) {
  if (!s_lvgl.pub.initialized)
    return;
  if (enable && !s_lvgl.pub.full_buf && s_lvgl.pub.partial_buf) {
    EOS_LOGW("Cannot force full-frame fallback without shadow full buffer");
    return;
  }
  s_lvgl.pub.fallback_full = enable;
  if (s_lvgl.pub.display) {
    lv_obj_invalidate(lv_screen_active());
  }
}

bool eos_lvgl_is_full_fallback(void) { return s_lvgl.pub.fallback_full; }

int eos_lvgl_flush_full_frame(void) {
  if (!s_lvgl.pub.initialized || !s_lvgl.pub.full_buf)
    return -1;
  if (s_lvgl.pub.display) {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_lvgl.pub.display);
  }
  return eos_lvgl_write_full_frame_buf(&s_lvgl.pub, s_lvgl.pub.full_buf);
}

void eos_lvgl_set_dpad_group_nav(bool enable) {
  s_lvgl.pub.dpad_group_nav = enable;
}

void eos_lvgl_set_key_cb(eos_lvgl_key_cb_t cb, void *user_data) {
  s_lvgl.pub.key_cb = cb;
  s_lvgl.pub.key_cb_user_data = user_data;
}

bool eos_lvgl_poll_key(uint16_t *eos_key_out) {
  if (!s_lvgl.pub.initialized)
    return false;

  // Poll hardware in case LVGL indev timer hasn't run yet in this iteration
  poll_hardware_keyboard();
  if (s_lvgl.touch_gestures_enabled) {
    poll_hardware_touch();
  }

  if (s_lvgl.poll_key_head == s_lvgl.poll_key_tail)
    return false;

  uint16_t key = s_lvgl.poll_key_queue[s_lvgl.poll_key_tail];
  s_lvgl.poll_key_tail = (uint8_t)((s_lvgl.poll_key_tail + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  // Synchronize indev queue: key was consumed by application event loop
  s_lvgl.indev_key_tail = s_lvgl.indev_key_head;
  s_lvgl.send_release = false;
  if (eos_key_out) {
    *eos_key_out = key;
  }
  return true;
}

void eos_lvgl_clear_keys(void) {
  s_lvgl.indev_key_head = 0;
  s_lvgl.indev_key_tail = 0;
  s_lvgl.poll_key_head = 0;
  s_lvgl.poll_key_tail = 0;
  s_lvgl.send_release = false;
}

void eos_lvgl_set_touch_cb(eos_lvgl_touch_cb_t cb, void *user_data) {
  s_lvgl.touch_cb = cb;
  s_lvgl.touch_cb_user_data = user_data;
}

bool eos_lvgl_poll_touch(eos_lvgl_touch_event_t *touch_out) {
  if (!s_lvgl.pub.initialized)
    return false;

  poll_hardware_touch();

  if (s_lvgl.touch_head == s_lvgl.touch_tail)
    return false;

  eos_lvgl_touch_event_t ev = s_lvgl.touch_queue[s_lvgl.touch_tail];
  s_lvgl.touch_tail =
      (uint8_t)((s_lvgl.touch_tail + 1U) % EOS_LVGL_TOUCH_QUEUE_SIZE);
  if (touch_out) {
    *touch_out = ev;
  }
  return true;
}

void eos_lvgl_clear_touch(void) {
  s_lvgl.touch_head = 0;
  s_lvgl.touch_tail = 0;
}

bool eos_lvgl_get_touch_point(uint16_t *x, uint16_t *y, bool *pressed) {
  if (!s_lvgl.pub.initialized)
    return false;
  poll_hardware_touch();
  if (x)
    *x = s_lvgl.last_touch_x;
  if (y)
    *y = s_lvgl.last_touch_y;
  if (pressed)
    *pressed = s_lvgl.touch_pressed;
  return s_lvgl.pub.touch_fd >= 0;
}

bool eos_lvgl_get_touch_point_ex(uint16_t *x, uint16_t *y, uint16_t *pressure,
                                 bool *pressed) {
  if (!s_lvgl.pub.initialized)
    return false;
  poll_hardware_touch();
  if (x)
    *x = s_lvgl.last_touch_x;
  if (y)
    *y = s_lvgl.last_touch_y;
  if (pressure)
    *pressure = s_lvgl.touch_pressed ? s_lvgl.last_touch_pressure : 0;
  if (pressed)
    *pressed = s_lvgl.touch_pressed;
  return s_lvgl.pub.touch_fd >= 0;
}

uint16_t eos_lvgl_get_touch_pressure(void) {
  if (!s_lvgl.pub.initialized)
    return 0;
  poll_hardware_touch();
  return s_lvgl.touch_pressed ? s_lvgl.last_touch_pressure : 0;
}

bool eos_lvgl_has_touch(void) {
  return s_lvgl.pub.initialized && s_lvgl.pub.touch_fd >= 0;
}

bool eos_lvgl_has_keyboard(void) {
  return s_lvgl.pub.initialized && s_lvgl.pub.kbd_fd >= 0;
}

void eos_lvgl_set_touch_gestures(bool enable) {
  s_lvgl.touch_gestures_enabled = enable;
}

bool eos_lvgl_get_touch_gestures(void) {
  return s_lvgl.touch_gestures_enabled;
}

static void eos_lvgl_default_back_btn_cb(lv_event_t *e) {
  (void)e;
  key_queue_push(KEY_ESC);
}

typedef struct {
  int32_t lh;
  int32_t unit;
  int32_t pad_xs;
  int32_t pad_sm;
  int32_t pad_md;
  int32_t pad_lg;
  int32_t pad_xl;
  int32_t radius_sm;
  int32_t radius_md;
  int32_t border_focus;
  int32_t hdr_h;
  int32_t btn_h;
  int32_t dlg_btn_h;
} eos_lvgl_metrics_t;

static eos_lvgl_metrics_t eos_lvgl_get_metrics(void) {
  const lv_font_t *font = eos_lvgl_get_font();
  if (!font) font = LV_FONT_DEFAULT;
  int32_t lh = (int32_t)lv_font_get_line_height(font);
  int32_t unit = lh / 4;
  if (unit < 2) unit = 2;
  eos_lvgl_metrics_t m = {
      .lh = lh,
      .unit = unit,
      .pad_xs = unit / 2,
      .pad_sm = unit,
      .pad_md = unit + unit / 2,
      .pad_lg = unit * 2,
      .pad_xl = unit * 3,
      .radius_sm = unit > 1 ? unit - 1 : 1,
      .radius_md = unit,
      .border_focus = unit / 2,
      .hdr_h = lh + unit * 2,
      .btn_h = lh + unit * 2,
      .dlg_btn_h = lh + unit * 4,
  };
  return m;
}

lv_obj_t *eos_lvgl_create_back_btn(lv_obj_t *parent, lv_event_cb_t custom_cb,
                                   void *user_data) {
  if (!parent) {
    parent = lv_screen_active();
  }
  if (!parent)
    return NULL;

  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  lv_obj_t *btn = lv_button_create(parent);
  int32_t ph = lv_obj_get_height(parent);
  if (ph > 0 && ph <= m.hdr_h + m.pad_xs) {
    lv_obj_set_size(btn, m.hdr_h, ph > m.pad_sm ? ph - m.pad_sm : m.lh);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -m.pad_xs, 0);
  } else {
    lv_obj_set_size(btn, m.hdr_h + m.pad_xs, m.lh + m.pad_xs);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -m.radius_sm, m.pad_xs);
  }
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_radius(btn, m.radius_sm, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl);

  lv_event_cb_t cb = custom_cb ? custom_cb : eos_lvgl_default_back_btn_cb;
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

  return btn;
}

// ── Reusable UI Components ─────────────────────────────────────

lv_obj_t *eos_lvgl_create_card(lv_obj_t *parent, int32_t w, int32_t h) {
  if (!parent) parent = lv_screen_active();
  if (!parent) return NULL;

  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, lv_color_hex(EOS_COLOR_BG_CARD), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(EOS_COLOR_BORDER), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, m.radius_md, 0);
  lv_obj_set_style_pad_all(card, m.pad_sm, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

lv_obj_t *eos_lvgl_create_header(lv_obj_t *parent, const char *title, const char *mode_text) {
  if (!parent) parent = lv_screen_active();
  if (!parent) return NULL;

  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  uint32_t w = eos_lvgl_get_width();
  lv_obj_t *hdr = lv_obj_create(parent);
  lv_obj_set_size(hdr, (int32_t)w, m.hdr_h);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(EOS_COLOR_BG_HDR), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_hor(hdr, m.pad_md, 0);
  lv_obj_set_style_pad_ver(hdr, m.pad_xs, 0);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  if (title) {
    lv_obj_t *lbl = lv_label_create(hdr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
  }

  if (mode_text) {
    lv_obj_t *badge = lv_label_create(hdr);
    lv_label_set_text(badge, mode_text);
    lv_obj_set_style_text_color(badge, lv_color_hex(EOS_COLOR_STATUS_GREEN), 0);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  return hdr;
}

lv_obj_t *eos_lvgl_create_toast(lv_obj_t *parent, const char *message, bool is_error) {
  if (!parent) parent = lv_screen_active();
  if (!parent || !message) return NULL;

  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  uint32_t w = eos_lvgl_get_width();
  int32_t toast_w = (int32_t)w - m.pad_xl * 2;
  if (toast_w < 0) toast_w = (int32_t)w;

  lv_obj_t *toast = lv_obj_create(parent);
  lv_obj_set_size(toast, toast_w, m.hdr_h + m.pad_xs);
  lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, m.hdr_h + m.pad_sm);
  lv_obj_set_style_bg_color(toast, lv_color_hex(is_error ? EOS_COLOR_STATUS_RED : EOS_COLOR_STATUS_GREEN), 0);
  lv_obj_set_style_border_width(toast, 0, 0);
  lv_obj_set_style_radius(toast, m.radius_md, 0);
  lv_obj_set_style_pad_all(toast, m.pad_xs, 0);
  lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(toast);
  lv_label_set_text(lbl, message);
  lv_obj_set_style_text_color(lbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
  lv_obj_center(lbl);

  return toast;
}

// ── Shared On-Screen Keyboard ───────────────────────────────────

static void eos_keyboard_delete_cb(lv_event_t *e) {
  if (s_lvgl.active_keyboard == lv_event_get_target(e)) {
    s_lvgl.active_keyboard = NULL;
  }
}

lv_obj_t *eos_lvgl_create_keyboard(lv_obj_t *parent, lv_obj_t *target_ta) {
  if (!parent) {
    parent = lv_screen_active();
  }
  if (!parent)
    return NULL;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  lv_obj_t *kb = lv_keyboard_create(parent);
  if (!kb)
    return NULL;

  if (target_ta) {
    lv_keyboard_set_textarea(kb, target_ta);
  }

  int32_t parent_w = lv_obj_get_width(parent);
  if (parent_w <= 0) parent_w = (int32_t)w;
  int32_t parent_h = lv_obj_get_height(parent);
  if (parent_h <= 0) parent_h = (int32_t)h;

  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  int32_t kb_w = parent_w - m.pad_md;
  if (kb_w < 0) kb_w = parent_w;
  int32_t kb_h = parent_h * EOS_LVGL_KB_HEIGHT_PCT / EOS_PERCENT_MAX;
  if (kb_h < EOS_LVGL_KB_MIN_H) kb_h = EOS_LVGL_KB_MIN_H;
  if (kb_h > parent_h - m.hdr_h) kb_h = parent_h - m.hdr_h;

  lv_obj_set_size(kb, kb_w, kb_h);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -m.pad_xs);
  lv_obj_set_style_bg_color(kb, lv_color_hex(EOS_COLOR_BG_PANEL), 0);
  lv_obj_set_style_border_color(kb, lv_color_hex(EOS_COLOR_BORDER), 0);
  lv_obj_set_style_border_width(kb, 1, 0);
  lv_obj_set_style_radius(kb, m.radius_md, 0);
  lv_obj_set_style_pad_all(kb, m.pad_xs, 0);
  lv_obj_set_style_pad_gap(kb, m.pad_xs, 0);

  // Normal keyboard keys
  lv_obj_set_style_bg_color(kb, lv_color_hex(EOS_COLOR_BG_CARD), LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_hex(EOS_COLOR_TEXT_PRIMARY), LV_PART_ITEMS);
  lv_obj_set_style_border_color(kb, lv_color_hex(EOS_COLOR_BORDER), LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, m.radius_sm, LV_PART_ITEMS);

  // High-visibility focus indicator for D-pad / arrow key navigation
  lv_obj_set_style_bg_color(kb, lv_color_hex(EOS_COLOR_ACCENT_BLUE),
                             LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(kb, lv_color_hex(EOS_COLOR_ACCENT_YELLOW),
                                LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(kb, 2,
                                LV_PART_ITEMS | LV_STATE_FOCUSED);

  // Initial button selection for immediate visibility
  lv_buttonmatrix_set_selected_button(kb, 0);
  lv_obj_add_state(kb, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);

  // Set active keyboard pointer & cleanup callback
  s_lvgl.active_keyboard = kb;
  lv_obj_add_event_cb(kb, eos_keyboard_delete_cb, LV_EVENT_DELETE, NULL);

  // Group integration: enable editing mode so arrow keys traverse keys directly
  lv_group_t *grp = eos_lvgl_get_group();
  if (grp) {
    lv_group_add_obj(grp, kb);
    lv_group_focus_obj(kb);
    lv_group_set_editing(grp, true);
  }

  return kb;
}

typedef struct {
  eos_lvgl_input_dialog_cfg_t cfg;
  lv_obj_t *overlay;
  lv_obj_t *textarea;
} eos_input_dialog_data_t;

static void input_dialog_delete_cb(lv_event_t *e) {
  eos_input_dialog_data_t *data = (eos_input_dialog_data_t *)lv_event_get_user_data(e);
  if (data) free(data);
}

static void input_dialog_ok_click(lv_event_t *e) {
  eos_input_dialog_data_t *data = (eos_input_dialog_data_t *)lv_event_get_user_data(e);
  if (!data) return;
  char text_copy[EOS_BUFFER_128_LEN] = {0};
  const char *t = lv_textarea_get_text(data->textarea);
  if (t) strncpy(text_copy, t, sizeof(text_copy) - 1);
  eos_lvgl_input_cb_t cb = data->cfg.ok_cb;
  void *ud = data->cfg.user_data;
  lv_obj_t *ov = data->overlay;
  lv_obj_delete(ov);
  if (cb) cb(text_copy, ud);
}

static void input_dialog_cancel_click(lv_event_t *e) {
  eos_input_dialog_data_t *data = (eos_input_dialog_data_t *)lv_event_get_user_data(e);
  if (!data) return;
  eos_lvgl_input_cb_t cb = data->cfg.cancel_cb;
  void *ud = data->cfg.user_data;
  lv_obj_t *ov = data->overlay;
  lv_obj_delete(ov);
  if (cb) cb(NULL, ud);
}

static void input_dialog_kb_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    input_dialog_ok_click(e);
  } else if (code == LV_EVENT_CANCEL) {
    input_dialog_cancel_click(e);
  }
}

static void input_dialog_build_header(lv_obj_t *card, int32_t dlg_btn_w, int32_t dlg_btn_h,
                                      const eos_lvgl_metrics_t *m,
                                      const eos_lvgl_input_dialog_cfg_t *cfg,
                                      eos_input_dialog_data_t *data,
                                      lv_obj_t **btn_ok_out, lv_obj_t **btn_cancel_out) {
  lv_obj_t *tlbl = lv_label_create(card);
  lv_label_set_text(tlbl, cfg->title ? cfg->title : "Input");
  lv_obj_set_style_text_color(tlbl, lv_color_hex(EOS_COLOR_ACCENT_CYAN), 0);
  lv_obj_align(tlbl, LV_ALIGN_TOP_LEFT, m->pad_sm, m->pad_sm);

  lv_obj_t *btn_cancel = lv_button_create(card);
  lv_obj_set_size(btn_cancel, dlg_btn_w, dlg_btn_h);
  lv_obj_align(btn_cancel, LV_ALIGN_TOP_RIGHT, -(dlg_btn_w + m->pad_lg), 0);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(EOS_COLOR_BG_PANEL), 0);
  lv_obj_set_style_border_color(btn_cancel, lv_color_hex(EOS_COLOR_BORDER), 0);
  lv_obj_set_style_border_width(btn_cancel, 1, 0);
  lv_obj_set_style_radius(btn_cancel, m->radius_sm, 0);
  lv_obj_add_event_cb(btn_cancel, input_dialog_cancel_click, LV_EVENT_CLICKED, data);

  lv_obj_t *clbl = lv_label_create(btn_cancel);
  lv_label_set_text(clbl, cfg->cancel_label ? cfg->cancel_label : "Cancel");
  lv_obj_set_style_text_color(clbl, lv_color_hex(EOS_COLOR_TEXT_MUTED), 0);
  lv_obj_center(clbl);

  lv_obj_t *btn_ok = lv_button_create(card);
  lv_obj_set_size(btn_ok, dlg_btn_w, dlg_btn_h);
  lv_obj_align(btn_ok, LV_ALIGN_TOP_RIGHT, -m->pad_xs, 0);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(EOS_COLOR_ACCENT_BLUE), 0);
  lv_obj_set_style_radius(btn_ok, m->radius_sm, 0);
  lv_obj_add_event_cb(btn_ok, input_dialog_ok_click, LV_EVENT_CLICKED, data);

  lv_obj_t *oklbl = lv_label_create(btn_ok);
  lv_label_set_text(oklbl, cfg->ok_label ? cfg->ok_label : "OK");
  lv_obj_set_style_text_color(oklbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
  lv_obj_center(oklbl);
  if (btn_ok_out) *btn_ok_out = btn_ok;
  if (btn_cancel_out) *btn_cancel_out = btn_cancel;
}

lv_obj_t *eos_lvgl_create_input_dialog(const eos_lvgl_input_dialog_cfg_t *cfg) {
  if (!cfg) return NULL;
  lv_obj_t *scr = lv_screen_active();
  if (!scr) return NULL;

  eos_input_dialog_data_t *data = eos_psram_calloc_prefer(1, sizeof(eos_input_dialog_data_t));
  if (!data) return NULL;
  data->cfg = *cfg;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  // Dimmed modal overlay
  lv_obj_t *ov = lv_obj_create(scr);
  data->overlay = ov;
  lv_obj_set_size(ov, (int32_t)w, (int32_t)h);
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_70, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ov, input_dialog_delete_cb, LV_EVENT_DELETE, data);

  // Dialog card box: on small displays (such as Lilka / QEMU 280x224),
  // card takes full available dimensions so keyboard keys are maximally enlarged.
  int32_t dlg_w = (int32_t)w;
  int32_t dlg_h = (int32_t)h;
  if (dlg_w > 440) dlg_w = 440;
  if (dlg_h > 360) dlg_h = 360;

  lv_obj_t *card = lv_obj_create(ov);
  lv_obj_set_size(card, dlg_w, dlg_h);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(EOS_COLOR_BG_CARD), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(EOS_COLOR_BORDER_FOCUS), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  int32_t dlg_btn_w = dlg_w / 4;
  int32_t dlg_btn_h = m.lh + m.pad_md;
  lv_obj_set_style_radius(card, m.radius_md, 0);
  lv_obj_set_style_pad_all(card, m.pad_sm, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Top header: Title on left, Action buttons on right
  lv_obj_t *btn_ok = NULL;
  lv_obj_t *btn_cancel = NULL;
  input_dialog_build_header(card, dlg_btn_w, dlg_btn_h, &m, cfg, data, &btn_ok, &btn_cancel);

  // Text Area right under header
  lv_obj_t *ta = lv_textarea_create(card);
  data->textarea = ta;
  int32_t ta_h = m.lh + m.pad_xl;
  int32_t ta_y = dlg_btn_h + m.pad_sm;
  lv_obj_set_size(ta, dlg_w - m.pad_xl, ta_h);
  lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, ta_y);
  if (cfg->is_password) {
    lv_textarea_set_password_mode(ta, true);
  }
  lv_textarea_set_one_line(ta, true);
  if (cfg->initial_text) {
    lv_textarea_set_text(ta, cfg->initial_text);
  } else if (cfg->prompt) {
    lv_textarea_set_placeholder_text(ta, cfg->prompt);
  }
  lv_obj_set_style_bg_color(ta, lv_color_hex(EOS_COLOR_BG_DARK), 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(EOS_COLOR_BORDER), 0);
  lv_obj_set_style_pad_all(ta, m.pad_sm, 0);

  // On-screen keyboard: ALWAYS created and maximized in remaining height
  int32_t kb_y = ta_y + ta_h + m.pad_sm;
  int32_t kb_h = dlg_h - kb_y - m.pad_sm;
  if (kb_h < EOS_LVGL_DIALOG_MIN_KB_H) kb_h = EOS_LVGL_DIALOG_MIN_KB_H;

  lv_obj_t *kb = eos_lvgl_create_keyboard(card, ta);
  if (kb) {
    lv_obj_set_size(kb, dlg_w - m.pad_md, kb_h);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, kb_y);
    lv_obj_add_event_cb(kb, input_dialog_kb_event, LV_EVENT_ALL, data);
  }

  // Register with group for hardware keys / D-pad navigation
  lv_group_t *grp = eos_lvgl_get_group();
  if (grp) {
    lv_group_add_obj(grp, ta);
    if (kb) lv_group_add_obj(grp, kb);
    lv_group_add_obj(grp, btn_ok);
    lv_group_add_obj(grp, btn_cancel);
    if (kb) {
      lv_group_focus_obj(kb);
      lv_group_set_editing(grp, true);
    }
  }

  return ov;
}

void eos_lvgl_close_input_dialog(lv_obj_t *dialog) {
  if (dialog) {
    lv_obj_delete(dialog);
  }
}

// ── Statusbar & Toolbar APIs ───────────────────────────────────

int eos_toolbar_set(const char *title, const char *shortcuts) {
  int fd = open("/tmp/tbar.tmp", O_WRONLY | O_CREAT | O_TRUNC, EOS_DEFAULT_FILE_MODE);
  if (fd < 0) return -1;
  char buf[EOS_MID_STR_LEN];
  int len = snprintf(buf, sizeof(buf), "@%ld %s\n%s\n", (long)getpid(),
                     title ? title : "",
                     shortcuts ? shortcuts : "");
  if (len > 0) {
    write(fd, buf, (size_t)len);
  }
  close(fd);
  return rename("/tmp/tbar.tmp", "/tmp/tbar");
}

int eos_toolbar_printf(const char *shortcuts, const char *fmt, ...) {
  char title[EOS_BUFFER_128_LEN] = {0};
  va_list args;
  va_start(args, fmt);
  vsnprintf(title, sizeof(title), fmt, args);
  va_end(args);
  return eos_toolbar_set(title, shortcuts);
}

int eos_toolbar_clear(void) {
  unlink("/tmp/tbar.tmp");
  return unlink("/tmp/tbar");
}

int eos_statusbar_set(const char *line1, const char *line2) {
  int fd = open("/tmp/sbar.tmp", O_WRONLY | O_CREAT | O_TRUNC, EOS_DEFAULT_FILE_MODE);
  if (fd < 0) return -1;
  char buf[EOS_MID_STR_LEN];
  int len = snprintf(buf, sizeof(buf), "@%ld %s\n%s\n", (long)getpid(),
                     line1 ? line1 : "",
                     line2 ? line2 : "");
  if (len > 0) {
    write(fd, buf, (size_t)len);
  }
  close(fd);
  return rename("/tmp/sbar.tmp", "/tmp/sbar");
}

int eos_statusbar_printf(const char *line2, const char *fmt, ...) {
  char line1[EOS_BUFFER_128_LEN] = {0};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line1, sizeof(line1), fmt, args);
  va_end(args);
  return eos_statusbar_set(line1, line2);
}

int eos_statusbar_clear(void) {
  unlink("/tmp/sbar.tmp");
  return unlink("/tmp/sbar");
}

// ── Shared Dialog Key/Hint Helpers ────────────────────────────

typedef enum {
  EOS_DIALOG_MAGIC = 0x454F5344u,
} eos_dialog_magic_t;

typedef enum {
  EOS_DIALOG_KIND_CONFIRM = 1,
  EOS_DIALOG_KIND_INFO,
  EOS_DIALOG_KIND_ALERT,
  EOS_DIALOG_KIND_PREVIEW,
} eos_dialog_kind_t;

typedef struct {
  uint32_t magic;
  eos_dialog_kind_t kind;
  lv_obj_t *overlay;
} eos_dialog_hdr_t;

static void dialog_dismiss_overlay(lv_obj_t *ov) {
  if (!ov) return;
  lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
  lv_obj_delete_async(ov);
}

static void format_btn_hint(char *out, size_t sz, const char *prefix,
                            const char *label, const char *fallback) {
  const char *text = label ? label : fallback;
  if (strncmp(text, prefix, strlen(prefix)) == 0) {
    strlcpy(out, text, sz);
  } else {
    snprintf(out, sz, "%s%s", prefix, text);
  }
}

// ── Shared Confirmation Dialog ─────────────────────────────────

typedef struct {
  eos_dialog_hdr_t hdr;
  eos_lvgl_confirm_dialog_cfg_t cfg;
} eos_confirm_dialog_data_t;

static void confirm_dialog_delete_cb(lv_event_t *e) {
  eos_confirm_dialog_data_t *data = (eos_confirm_dialog_data_t *)lv_event_get_user_data(e);
  if (data) free(data);
}

static void confirm_dialog_finish(eos_confirm_dialog_data_t *data, bool confirmed) {
  if (!data) return;
  eos_lvgl_confirm_cb_t cb = data->cfg.cb;
  void *ud = data->cfg.user_data;
  dialog_dismiss_overlay(data->hdr.overlay);
  if (cb) cb(confirmed, ud);
}

static void confirm_dialog_confirm_click(lv_event_t *e) {
  confirm_dialog_finish((eos_confirm_dialog_data_t *)lv_event_get_user_data(e), true);
}

static void confirm_dialog_cancel_click(lv_event_t *e) {
  confirm_dialog_finish((eos_confirm_dialog_data_t *)lv_event_get_user_data(e), false);
}

lv_obj_t *eos_lvgl_dialog_confirm(const eos_lvgl_confirm_dialog_cfg_t *cfg) {
  if (!cfg) return NULL;
  lv_obj_t *scr = lv_screen_active();
  if (!scr) return NULL;

  eos_confirm_dialog_data_t *data = eos_psram_calloc_prefer(1, sizeof(eos_confirm_dialog_data_t));
  if (!data) return NULL;
  data->hdr.magic = EOS_DIALOG_MAGIC;
  data->hdr.kind = EOS_DIALOG_KIND_CONFIRM;
  data->cfg = *cfg;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  lv_obj_t *ov = lv_obj_create(scr);
  data->hdr.overlay = ov;
  lv_obj_set_user_data(ov, data);
  lv_obj_set_size(ov, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_70, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_pad_all(ov, 0, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ov, confirm_dialog_delete_cb, LV_EVENT_DELETE, data);

  int32_t dlg_w = (int32_t)w;

  lv_obj_t *card = lv_obj_create(ov);
  lv_obj_set_size(card, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(EOS_COLOR_BG_CARD), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(EOS_COLOR_BORDER_FOCUS), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  int32_t content_w = dlg_w - m.pad_xl * 2;
  lv_obj_set_style_pad_all(card, m.pad_xl, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Title
  lv_obj_t *tlbl = lv_label_create(card);
  lv_label_set_text(tlbl, cfg->title ? cfg->title : "Confirm");
  lv_obj_set_style_text_color(tlbl, lv_color_hex(EOS_COLOR_ACCENT_CYAN), 0);
  lv_obj_set_pos(tlbl, 0, 0);

  // Message body
  if (cfg->message) {
    lv_obj_t *mlbl = lv_label_create(card);
    lv_label_set_text(mlbl, cfg->message);
    lv_obj_set_width(mlbl, content_w);
    lv_label_set_long_mode(mlbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(mlbl, lv_color_hex(EOS_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_pos(mlbl, 0, m.lh + m.pad_xl);
  }

  // Button row pinned at bottom
  lv_obj_t *btn_row = lv_obj_create(card);
  lv_obj_set_size(btn_row, content_w, m.dlg_btn_h + m.pad_xs);
  lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  // Cancel button
  lv_obj_t *btn_c = lv_button_create(btn_row);
  lv_obj_set_size(btn_c, lv_pct(EOS_LVGL_SPLIT_BTN_PCT), m.dlg_btn_h);
  lv_obj_align(btn_c, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_c, lv_color_hex(EOS_COLOR_BG_PANEL), 0);
  lv_obj_set_style_border_color(btn_c, lv_color_hex(EOS_COLOR_BORDER), 0);
  lv_obj_set_style_border_width(btn_c, 1, 0);
  lv_obj_set_style_radius(btn_c, m.radius_md, 0);
  lv_obj_add_event_cb(btn_c, confirm_dialog_cancel_click, LV_EVENT_CLICKED, data);

  char clbl_buf[EOS_SMALL_STR_LEN];
  format_btn_hint(clbl_buf, sizeof(clbl_buf), "B:", cfg->cancel_label, "Cancel");
  lv_obj_t *clbl = lv_label_create(btn_c);
  lv_label_set_text(clbl, clbl_buf);
  lv_obj_set_style_text_color(clbl, lv_color_hex(EOS_COLOR_TEXT_MUTED), 0);
  lv_obj_center(clbl);

  // Confirm button
  lv_obj_t *btn_ok = lv_button_create(btn_row);
  lv_obj_set_size(btn_ok, lv_pct(EOS_LVGL_SPLIT_BTN_PCT), m.dlg_btn_h);
  lv_obj_align(btn_ok, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(cfg->dangerous ? EOS_COLOR_STATUS_RED : EOS_COLOR_ACCENT_BLUE), 0);
  lv_obj_set_style_radius(btn_ok, m.radius_sm, 0);
  lv_obj_add_event_cb(btn_ok, confirm_dialog_confirm_click, LV_EVENT_CLICKED, data);

  char oklbl_buf[EOS_SMALL_STR_LEN];
  format_btn_hint(oklbl_buf, sizeof(oklbl_buf), "START:", cfg->confirm_label, "Confirm");
  lv_obj_t *oklbl = lv_label_create(btn_ok);
  lv_label_set_text(oklbl, oklbl_buf);
  lv_obj_set_style_text_color(oklbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
  lv_obj_center(oklbl);

  lv_group_t *grp = eos_lvgl_get_group();
  if (grp) {
    lv_group_add_obj(grp, btn_c);
    lv_group_add_obj(grp, btn_ok);
    lv_group_focus_obj(btn_ok);
  }

  return ov;
}

// ── Shared Information Dialog ──────────────────────────────────

typedef struct {
  eos_dialog_hdr_t hdr;
  eos_lvgl_info_dialog_cfg_t cfg;
} eos_info_dialog_data_t;

static void info_dialog_delete_cb(lv_event_t *e) {
  eos_info_dialog_data_t *data = (eos_info_dialog_data_t *)lv_event_get_user_data(e);
  if (data) free(data);
}

static void info_dialog_finish(eos_info_dialog_data_t *data) {
  if (!data) return;
  eos_lvgl_info_cb_t cb = data->cfg.cb;
  void *ud = data->cfg.user_data;
  dialog_dismiss_overlay(data->hdr.overlay);
  if (cb) cb(ud);
}

static void info_dialog_ok_click(lv_event_t *e) {
  info_dialog_finish((eos_info_dialog_data_t *)lv_event_get_user_data(e));
}

lv_obj_t *eos_lvgl_dialog_info(const eos_lvgl_info_dialog_cfg_t *cfg) {
  if (!cfg) return NULL;
  lv_obj_t *scr = lv_screen_active();
  if (!scr) return NULL;

  eos_info_dialog_data_t *data = eos_psram_calloc_prefer(1, sizeof(eos_info_dialog_data_t));
  if (!data) return NULL;
  data->hdr.magic = EOS_DIALOG_MAGIC;
  data->hdr.kind = EOS_DIALOG_KIND_INFO;
  data->cfg = *cfg;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  lv_obj_t *ov = lv_obj_create(scr);
  data->hdr.overlay = ov;
  lv_obj_set_user_data(ov, data);
  lv_obj_set_size(ov, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_70, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_pad_all(ov, 0, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ov, info_dialog_delete_cb, LV_EVENT_DELETE, data);

  int32_t dlg_w = (int32_t)w;

  lv_obj_t *card = lv_obj_create(ov);
  lv_obj_set_size(card, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(EOS_COLOR_BG_CARD), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(EOS_COLOR_BORDER_FOCUS), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  int32_t content_w = dlg_w - m.pad_xl * 2;
  lv_obj_set_style_pad_all(card, m.pad_xl, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Title
  lv_obj_t *tlbl = lv_label_create(card);
  char thdr[EOS_LINE_STR_LEN];
  snprintf(thdr, sizeof(thdr), "i  %s", cfg->title ? cfg->title : "Information");
  lv_label_set_text(tlbl, thdr);
  lv_obj_set_style_text_color(tlbl, lv_color_hex(EOS_COLOR_ACCENT_CYAN), 0);
  lv_obj_set_pos(tlbl, 0, 0);

  // Message body
  if (cfg->message) {
    lv_obj_t *mlbl = lv_label_create(card);
    lv_label_set_text(mlbl, cfg->message);
    lv_obj_set_width(mlbl, content_w);
    lv_label_set_long_mode(mlbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(mlbl, lv_color_hex(EOS_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_pos(mlbl, 0, m.lh + m.pad_xl);
  }

  // Dismiss Button
  lv_obj_t *btn = lv_button_create(card);
  lv_obj_set_size(btn, lv_pct(EOS_PERCENT_MAX), m.dlg_btn_h);
  lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_COLOR_ACCENT_BLUE), 0);
  lv_obj_set_style_radius(btn, m.radius_md, 0);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(btn, info_dialog_ok_click, LV_EVENT_CLICKED, data);

  char blbl_buf[EOS_SMALL_STR_LEN];
  format_btn_hint(blbl_buf, sizeof(blbl_buf), "A:", cfg->button_label, "OK");
  lv_obj_t *blbl = lv_label_create(btn);
  lv_label_set_text(blbl, blbl_buf);
  lv_obj_set_style_text_color(blbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
  lv_obj_center(blbl);

  lv_group_t *grp = eos_lvgl_get_group();
  if (grp) {
    lv_group_add_obj(grp, btn);
    lv_group_focus_obj(btn);
  }

  return ov;
}

// ── Shared Alert Dialog ────────────────────────────────────────

typedef struct {
  eos_dialog_hdr_t hdr;
  eos_lvgl_alert_dialog_cfg_t cfg;
} eos_alert_dialog_data_t;

static void alert_dialog_delete_cb(lv_event_t *e) {
  eos_alert_dialog_data_t *data = (eos_alert_dialog_data_t *)lv_event_get_user_data(e);
  if (data) free(data);
}

static void alert_dialog_finish(eos_alert_dialog_data_t *data, bool ok) {
  if (!data) return;
  eos_lvgl_alert_cb_t cb = data->cfg.cb;
  void *ud = data->cfg.user_data;
  dialog_dismiss_overlay(data->hdr.overlay);
  if (cb) cb(ok, ud);
}

static void alert_dialog_ok_click(lv_event_t *e) {
  alert_dialog_finish((eos_alert_dialog_data_t *)lv_event_get_user_data(e), true);
}

static void alert_dialog_cancel_click(lv_event_t *e) {
  alert_dialog_finish((eos_alert_dialog_data_t *)lv_event_get_user_data(e), false);
}

static void alert_dialog_build_buttons(lv_obj_t *card, uint32_t accent,
                                       const eos_lvgl_alert_dialog_cfg_t *cfg,
                                       eos_alert_dialog_data_t *data, lv_group_t *grp) {
  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  if (cfg->cancel_label) {
    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_set_size(btn_row, lv_pct(EOS_PERCENT_MAX), m.btn_h + m.pad_sm);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_c = lv_button_create(btn_row);
    lv_obj_set_size(btn_c, lv_pct(EOS_LVGL_SPLIT_BTN_PCT), m.btn_h);
    lv_obj_align(btn_c, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_c, lv_color_hex(EOS_COLOR_BG_PANEL), 0);
    lv_obj_set_style_border_color(btn_c, lv_color_hex(EOS_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(btn_c, 1, 0);
    lv_obj_set_style_radius(btn_c, m.radius_sm, 0);
    lv_obj_add_event_cb(btn_c, alert_dialog_cancel_click, LV_EVENT_CLICKED, data);

    char clbl_buf[EOS_SMALL_STR_LEN];
    format_btn_hint(clbl_buf, sizeof(clbl_buf), "B:", cfg->cancel_label, "Cancel");
    lv_obj_t *clbl = lv_label_create(btn_c);
    lv_label_set_text(clbl, clbl_buf);
    lv_obj_set_style_text_color(clbl, lv_color_hex(EOS_COLOR_TEXT_MUTED), 0);
    lv_obj_center(clbl);

    lv_obj_t *btn_ok = lv_button_create(btn_row);
    lv_obj_set_size(btn_ok, lv_pct(EOS_LVGL_SPLIT_BTN_PCT), m.btn_h);
    lv_obj_align(btn_ok, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(accent), 0);
    lv_obj_set_style_radius(btn_ok, m.radius_sm, 0);
    lv_obj_add_event_cb(btn_ok, alert_dialog_ok_click, LV_EVENT_CLICKED, data);

    char oklbl_buf[EOS_SMALL_STR_LEN];
    format_btn_hint(oklbl_buf, sizeof(oklbl_buf), "A:", cfg->ok_label, "Acknowledge");
    lv_obj_t *oklbl = lv_label_create(btn_ok);
    lv_label_set_text(oklbl, oklbl_buf);
    lv_obj_set_style_text_color(oklbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
    lv_obj_center(oklbl);

    if (grp) {
      lv_group_add_obj(grp, btn_c);
      lv_group_add_obj(grp, btn_ok);
      lv_group_focus_obj(btn_ok);
    }
  } else {
    lv_obj_t *btn = lv_button_create(card);
    lv_obj_set_size(btn, lv_pct(EOS_LVGL_HALF_PCT), m.btn_h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(accent), 0);
    lv_obj_set_style_radius(btn, m.radius_sm, 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, alert_dialog_ok_click, LV_EVENT_CLICKED, data);

    char blbl_buf[EOS_SMALL_STR_LEN];
    format_btn_hint(blbl_buf, sizeof(blbl_buf), "A:", cfg->ok_label, "Acknowledge");
    lv_obj_t *blbl = lv_label_create(btn);
    lv_label_set_text(blbl, blbl_buf);
    lv_obj_set_style_text_color(blbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
    lv_obj_center(blbl);

    if (grp) {
      lv_group_add_obj(grp, btn);
      lv_group_focus_obj(btn);
    }
  }
}

lv_obj_t *eos_lvgl_dialog_alert(const eos_lvgl_alert_dialog_cfg_t *cfg) {
  if (!cfg) return NULL;
  lv_obj_t *scr = lv_screen_active();
  if (!scr) return NULL;

  eos_alert_dialog_data_t *data = eos_psram_calloc_prefer(1, sizeof(eos_alert_dialog_data_t));
  if (!data) return NULL;
  data->hdr.magic = EOS_DIALOG_MAGIC;
  data->hdr.kind = EOS_DIALOG_KIND_ALERT;
  data->cfg = *cfg;

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  lv_obj_t *ov = lv_obj_create(scr);
  data->hdr.overlay = ov;
  lv_obj_set_user_data(ov, data);
  lv_obj_set_size(ov, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_70, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_pad_all(ov, 0, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ov, alert_dialog_delete_cb, LV_EVENT_DELETE, data);

  int32_t dlg_w = (int32_t)w;

  uint32_t accent = EOS_COLOR_ACCENT_CYAN;
  const char *icon = "i";
  if (cfg->level == EOS_LVGL_ALERT_WARNING) {
    accent = EOS_COLOR_STATUS_ORANGE;
    icon = "!";
  } else if (cfg->level == EOS_LVGL_ALERT_ERROR) {
    accent = EOS_COLOR_STATUS_RED;
    icon = "X";
  }

  lv_obj_t *card = lv_obj_create(ov);
  lv_obj_set_size(card, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(EOS_COLOR_BG_CARD), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(accent), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  int32_t content_w = dlg_w - m.pad_xl * 2;
  lv_obj_set_style_pad_all(card, m.pad_xl, 0);
  lv_obj_set_style_pad_row(card, m.pad_lg, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Header
  lv_obj_t *tlbl = lv_label_create(card);
  char thdr[EOS_LINE_STR_LEN];
  snprintf(thdr, sizeof(thdr), "%s  %s", icon, cfg->title ? cfg->title : "Alert");
  lv_label_set_text(tlbl, thdr);
  lv_obj_set_style_text_color(tlbl, lv_color_hex(accent), 0);

  // Message body
  if (cfg->message) {
    lv_obj_t *mlbl = lv_label_create(card);
    lv_label_set_text(mlbl, cfg->message);
    lv_obj_set_width(mlbl, content_w);
    lv_label_set_long_mode(mlbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(mlbl, lv_color_hex(EOS_COLOR_TEXT_PRIMARY), 0);
  }

  lv_group_t *grp = eos_lvgl_get_group();
  alert_dialog_build_buttons(card, accent, cfg, data, grp);

  return ov;
}

void eos_lvgl_dialog_close(lv_obj_t *dialog) {
  if (dialog) {
    dialog_dismiss_overlay(dialog);
  }
}

// ── Shared Preview Dialog ──────────────────────────────────────

typedef struct {
  eos_dialog_hdr_t hdr;
  eos_lvgl_preview_dialog_cfg_t cfg;
  lv_obj_t *card;
  lv_obj_t *ta;
  lv_obj_t *btn;
} eos_preview_dialog_data_t;

static void preview_dialog_delete_cb(lv_event_t *e) {
  eos_preview_dialog_data_t *data = (eos_preview_dialog_data_t *)lv_event_get_user_data(e);
  if (data) {
    if (data->cfg.cb) {
      data->cfg.cb(data->cfg.user_data);
    }
    free(data);
  }
  eos_lvgl_clear_keys();
}

static void preview_dialog_close_click(lv_event_t *e) {
  eos_preview_dialog_data_t *data = (eos_preview_dialog_data_t *)lv_event_get_user_data(e);
  if (data && data->hdr.overlay) {
    eos_lvgl_dialog_close(data->hdr.overlay);
  }
}

static void preview_dialog_key_cb(lv_event_t *e) {
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_ENTER) {
    eos_preview_dialog_data_t *data = (eos_preview_dialog_data_t *)lv_event_get_user_data(e);
    if (data && data->hdr.overlay) {
      eos_lvgl_dialog_close(data->hdr.overlay);
    }
  }
}

static bool preview_dialog_handle_key(eos_preview_dialog_data_t *data, uint16_t key) {
  switch (key) {
  case KEY_UP:
    if (data->ta) {
      lv_obj_scroll_by_bounded(data->ta, 0, 30, LV_ANIM_OFF);
    }
    return true;
  case KEY_DOWN:
    if (data->ta) {
      lv_obj_scroll_by_bounded(data->ta, 0, -30, LV_ANIM_OFF);
    }
    return true;
  case KEY_A:
  case KEY_B:
  case KEY_ENTER:
  case KEY_ESC:
  case KEY_BACKSPACE:
  case KEY_SPACE:
    eos_lvgl_dialog_close(data->hdr.overlay);
    return true;
  default:
    return false;
  }
}

bool eos_lvgl_dialog_handle_key(lv_obj_t *dialog, uint16_t key) {
  if (!dialog) return false;
  eos_dialog_hdr_t *hdr = (eos_dialog_hdr_t *)lv_obj_get_user_data(dialog);
  if (!hdr || hdr->magic != EOS_DIALOG_MAGIC || hdr->overlay != dialog) {
    return false;
  }
  if (hdr->kind == EOS_DIALOG_KIND_CONFIRM) {
    eos_confirm_dialog_data_t *data = (eos_confirm_dialog_data_t *)hdr;
    if (key == KEY_SPACE) {
      confirm_dialog_finish(data, true);
      return true;
    }
    if (key == KEY_B || key == KEY_ESC || key == KEY_BACKSPACE) {
      confirm_dialog_finish(data, false);
      return true;
    }
    return false;
  }
  if (hdr->kind == EOS_DIALOG_KIND_INFO) {
    if (key == KEY_A || key == KEY_B || key == KEY_ESC || key == KEY_BACKSPACE) {
      info_dialog_finish((eos_info_dialog_data_t *)hdr);
      return true;
    }
    return false;
  }
  if (hdr->kind == EOS_DIALOG_KIND_ALERT) {
    eos_alert_dialog_data_t *data = (eos_alert_dialog_data_t *)hdr;
    if (key == KEY_A) {
      alert_dialog_finish(data, true);
      return true;
    }
    if (key == KEY_B || key == KEY_ESC || key == KEY_BACKSPACE) {
      alert_dialog_finish(data, false);
      return true;
    }
    return false;
  }
  if (hdr->kind == EOS_DIALOG_KIND_PREVIEW) {
    return preview_dialog_handle_key((eos_preview_dialog_data_t *)hdr, key);
  }
  return false;
}

bool eos_lvgl_dialog_preview_handle_key(lv_obj_t *dialog, uint16_t key) {
  return eos_lvgl_dialog_handle_key(dialog, key);
}

lv_obj_t *eos_lvgl_dialog_preview(const eos_lvgl_preview_dialog_cfg_t *cfg) {
  if (!cfg) return NULL;
  lv_obj_t *scr = lv_screen_active();
  if (!scr) return NULL;

  eos_lvgl_clear_keys();

  eos_preview_dialog_data_t *data = eos_psram_calloc_prefer(1, sizeof(eos_preview_dialog_data_t));
  if (!data) return NULL;
  data->hdr.magic = EOS_DIALOG_MAGIC;
  data->hdr.kind = EOS_DIALOG_KIND_PREVIEW;
  data->cfg = *cfg;

  bool is_compact = eos_screen_is_compact();

  lv_obj_t *ov = lv_obj_create(scr);
  data->hdr.overlay = ov;
  lv_obj_set_user_data(ov, data);
  lv_obj_set_size(ov, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_70, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_pad_all(ov, 0, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ov, preview_dialog_delete_cb, LV_EVENT_DELETE, data);

  lv_obj_t *card = lv_obj_create(ov);
  data->card = card;
  lv_obj_set_size(card, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(EOS_COLOR_BG_CARD), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(EOS_COLOR_BORDER_FOCUS), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  lv_obj_set_style_pad_all(card, is_compact ? m.pad_sm : m.pad_lg, 0);
  lv_obj_set_style_pad_row(card, is_compact ? m.pad_sm : m.pad_md, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Title / Header
  lv_obj_t *tlbl = lv_label_create(card);
  char thdr[EOS_LINE_STR_LEN];
  snprintf(thdr, sizeof(thdr), "%s  %s", LV_SYMBOL_FILE, cfg->title ? cfg->title : "Preview");
  lv_label_set_text(tlbl, thdr);
  lv_obj_set_style_text_color(tlbl, lv_color_hex(EOS_COLOR_ACCENT_CYAN), 0);

  // Textarea body (scrollable and read-only)
  lv_obj_t *ta = lv_textarea_create(card);
  data->ta = ta;
  lv_obj_set_width(ta, lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_flex_grow(ta, 1);
  lv_textarea_set_cursor_click_pos(ta, false);
  lv_textarea_set_text(ta, cfg->content ? cfg->content : "");
  lv_obj_set_style_bg_color(ta, lv_color_hex(EOS_COLOR_BG_DARK), 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(EOS_COLOR_TEXT_PRIMARY), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(EOS_COLOR_BORDER), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_radius(ta, m.radius_md, 0);
  lv_obj_set_style_pad_all(ta, m.pad_sm, 0);

  // Close Button
  lv_obj_t *btn = lv_button_create(card);
  data->btn = btn;
  lv_obj_set_size(btn, lv_pct(EOS_LVGL_HALF_PCT), m.btn_h);
  lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_COLOR_ACCENT_BLUE), 0);
  lv_obj_set_style_radius(btn, m.radius_sm, 0);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(btn, preview_dialog_close_click, LV_EVENT_CLICKED, data);

  lv_obj_t *blbl = lv_label_create(btn);
  char btn_text[EOS_SMALL_STR_LEN];
  format_btn_hint(btn_text, sizeof(btn_text), "B:", cfg->close_label, "Close");
  lv_label_set_text(blbl, btn_text);
  lv_obj_set_style_text_color(blbl, lv_color_hex(EOS_COLOR_TEXT_WHITE), 0);
  lv_obj_center(blbl);

  // Keyboard / Keypad navigation
  lv_group_t *grp = eos_lvgl_get_group();
  if (grp) {
    lv_group_add_obj(grp, ta);
    lv_group_add_obj(grp, btn);
    lv_group_focus_obj(btn);
  }

  // Intercept ESC / Button B to dismiss dialog
  lv_obj_add_event_cb(ov, preview_dialog_key_cb, LV_EVENT_KEY, data);
  lv_obj_add_event_cb(card, preview_dialog_key_cb, LV_EVENT_KEY, data);
  lv_obj_add_event_cb(ta, preview_dialog_key_cb, LV_EVENT_KEY, data);
  lv_obj_add_event_cb(btn, preview_dialog_key_cb, LV_EVENT_KEY, data);

  return ov;
}

// ===================== Unified Menus (eos_menu & eos_popup_menu) =====================

typedef struct {
  lv_obj_t *root;
  lv_obj_t *card;
  eos_menu_cfg_t cfg;
  eos_menu_item_t *items;
  lv_obj_t **item_btns;
  lv_obj_t **item_labels;
  int focused_idx;
  bool is_popup;
  bool is_deleting;
} eos_menu_ctx_t;

static const char *eos_parse_ansi_color_tag(const char *src, size_t *consumed) {
  *consumed = 0;
  if (src[0] != '\033' || src[1] != '[') {
    return NULL;
  }

  size_t seq_start = 2;
  size_t seq_end = seq_start;
  while (src[seq_end] != '\0' && src[seq_end] != 'm' && (seq_end - seq_start) < EOS_NUM_STR_LEN) {
    seq_end++;
  }
  if (src[seq_end] != 'm') {
    return NULL;
  }

  *consumed = seq_end + 1;
  char seq[EOS_XSMALL_STR_LEN];
  size_t slen = seq_end - seq_start;
  if (slen >= sizeof(seq)) {
    return NULL;
  }
  memcpy(seq, &src[seq_start], slen);
  seq[slen] = '\0';

  if (strcmp(seq, "0") == 0) return "#";
  if (strstr(seq, "31")) return "#ff5555 ";
  if (strstr(seq, "32")) return "#55ff55 ";
  if (strstr(seq, "33")) return "#ffff55 ";
  if (strstr(seq, "34")) return "#5555ff ";
  if (strstr(seq, "35")) return "#ff55ff ";
  if (strstr(seq, "36")) return "#55ffff ";
  if (strstr(seq, "37")) return "#ffffff ";
  return NULL;
}

void eos_ansi_to_lvgl_recolor(const char *src, char *dst, size_t dst_len) {
  if (!dst || dst_len == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }

  size_t out_idx = 0;
  for (size_t in_idx = 0; src[in_idx] != '\0' && out_idx + 1 < dst_len; ) {
    size_t consumed = 0;
    const char *color_hex = eos_parse_ansi_color_tag(&src[in_idx], &consumed);
    if (color_hex && consumed > 0) {
      size_t clen = strlen(color_hex);
      if (out_idx + clen < dst_len) {
        memcpy(&dst[out_idx], color_hex, clen);
        out_idx += clen;
      }
      in_idx += consumed;
      continue;
    }
    dst[out_idx++] = src[in_idx++];
  }
  dst[out_idx] = '\0';
}

static void eos_menu_item_click_cb(lv_event_t *e);
static void eos_menu_item_focus_cb(lv_event_t *e);
static void eos_menu_item_key_cb(lv_event_t *e);

static void eos_menu_item_click_cb(lv_event_t *e) {
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_current_target(e);
  const eos_menu_item_t *item = (const eos_menu_item_t *)lv_obj_get_user_data(btn);
  if (!ctx || ctx->is_deleting || !item) return;

  if (ctx->item_btns) {
    for (size_t i = 0; i < ctx->cfg.item_count; i++) {
      if (ctx->item_btns[i] == btn) {
        eos_menu_focus_index(ctx->root, (int)i);
        break;
      }
    }
  }

  int item_id = item->id;
  eos_menu_item_cb_t action_cb = item->action_cb;
  bool is_popup = ctx->is_popup;
  lv_obj_t *root = ctx->root;

  if (action_cb) {
    action_cb((eos_menu_t *)ctx, item, ctx->cfg.user_data);
  } else if (ctx->cfg.on_select) {
    ctx->cfg.on_select((eos_menu_t *)ctx, item_id, ctx->cfg.user_data);
  }

  if (is_popup && root) {
    lv_obj_del_async(root);
  }
}

static void eos_menu_backdrop_click_cb(lv_event_t *e) {
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_event_get_user_data(e);
  lv_obj_t *target = lv_event_get_target(e);
  if (!ctx || ctx->is_deleting || target != ctx->root) return;

  if (ctx->cfg.on_cancel) {
    ctx->cfg.on_cancel((eos_menu_t *)ctx, ctx->cfg.user_data);
  }
  lv_obj_del_async(ctx->root);
}

static void eos_menu_context_cleanup_cb(lv_event_t *e) {
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_event_get_user_data(e);
  if (ctx) {
    ctx->is_deleting = true;
    lv_group_t *grp = eos_lvgl_get_group();
    if (ctx->item_btns) {
      for (size_t i = 0; i < ctx->cfg.item_count; i++) {
        lv_obj_t *btn = ctx->item_btns[i];
        if (btn) {
          lv_obj_remove_event_cb(btn, eos_menu_item_click_cb);
          lv_obj_remove_event_cb(btn, eos_menu_item_focus_cb);
          lv_obj_remove_event_cb(btn, eos_menu_item_key_cb);
          lv_obj_set_user_data(btn, NULL);
          ctx->item_btns[i] = NULL;
        }
      }
      free(ctx->item_btns);
      ctx->item_btns = NULL;
    }
    if (ctx->items) {
      free(ctx->items);
      ctx->items = NULL;
    }
    if (ctx->item_labels) {
      free(ctx->item_labels);
      ctx->item_labels = NULL;
    }
    free(ctx);
  }
}

static void eos_menu_item_focus_cb(lv_event_t *e) {
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (!ctx || ctx->is_deleting || !btn || !ctx->item_btns) return;

  for (size_t i = 0; i < ctx->cfg.item_count; i++) {
    if (ctx->item_btns[i] == btn) {
      if (ctx->focused_idx != (int)i) {
        eos_menu_focus_index(ctx->root, (int)i);
      }
      break;
    }
  }
}

static void eos_menu_item_key_cb(lv_event_t *e) {
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_event_get_user_data(e);
  if (!ctx || ctx->is_deleting) return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC) {
    if (ctx->cfg.on_cancel) {
      ctx->cfg.on_cancel((eos_menu_t *)ctx, ctx->cfg.user_data);
    }
    if (ctx->is_popup && ctx->root) {
      lv_obj_del_async(ctx->root);
    }
  }
}

static lv_obj_t *eos_menu_render_item_button(lv_obj_t *parent, const eos_menu_item_t *item,
                                              eos_menu_ctx_t *ctx, int32_t y, int32_t w, int32_t h,
                                              int32_t pad_h, int32_t icon_gap, lv_obj_t **lbl_out) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_pos(btn, 0, (lv_coord_t)y);
  lv_obj_set_size(btn, (lv_coord_t)w, (lv_coord_t)h);
  lv_obj_set_style_radius(btn, 0, 0);

  // Normal default state
  lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);

  // Focused / selected state (prominent highlight on small screens & keypad)
  lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PRIMARY_BLUE), LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PRIMARY_BLUE), LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_color(btn, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), LV_STATE_FOCUS_KEY);
  eos_lvgl_metrics_t m = eos_lvgl_get_metrics();
  lv_obj_set_style_border_width(btn, m.border_focus, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(btn, m.border_focus, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, LV_STATE_FOCUSED);
  lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUS_KEY);

  lv_obj_set_style_pad_hor(btn, (lv_coord_t)pad_h, 0);
  lv_obj_set_style_pad_ver(btn, 0, 0);
  lv_obj_set_user_data(btn, (void *)item);
  lv_obj_add_event_cb(btn, eos_menu_item_click_cb, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(btn, eos_menu_item_focus_cb, LV_EVENT_FOCUSED, ctx);
  lv_obj_add_event_cb(btn, eos_menu_item_key_cb, LV_EVENT_KEY, ctx);

  if (item->icon) {
    lv_obj_t *icon_lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(icon_lbl, eos_lvgl_get_font(), 0);
    lv_label_set_text(icon_lbl, item->icon);
    lv_obj_align(icon_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(icon_lbl, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t *txt_lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(txt_lbl, eos_lvgl_get_font(), 0);
  char recolor_buf[EOS_MID_STR_LEN];
  eos_ansi_to_lvgl_recolor(item->label ? item->label : "", recolor_buf, sizeof(recolor_buf));
  lv_label_set_recolor(txt_lbl, true);
  lv_label_set_text(txt_lbl, recolor_buf);
  lv_obj_align(txt_lbl, LV_ALIGN_LEFT_MID, item->icon ? (lv_coord_t)icon_gap : 0, 0);
  lv_obj_clear_flag(txt_lbl, LV_OBJ_FLAG_CLICKABLE);

  uint32_t text_col = item->is_destructive ? EOS_THEME_HIGHLIGHT_YELLOW : EOS_THEME_TEXT_PRIMARY;
  lv_obj_set_style_text_color(txt_lbl, lv_color_hex(text_col), 0);
  lv_obj_set_style_text_color(txt_lbl, lv_color_hex(EOS_THEME_TEXT_WHITE), LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(txt_lbl, lv_color_hex(EOS_THEME_TEXT_WHITE), LV_STATE_FOCUS_KEY);

  if (lbl_out) {
    *lbl_out = txt_lbl;
  }

  if (item->detail) {
    lv_obj_t *det_lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(det_lbl, eos_lvgl_get_font(), 0);
    lv_label_set_text(det_lbl, item->detail);
    lv_obj_set_style_text_color(det_lbl, lv_color_hex(EOS_THEME_TEXT_MUTED), 0);
    lv_obj_set_style_text_color(det_lbl, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(det_lbl, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), LV_STATE_FOCUS_KEY);
    lv_obj_align(det_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_clear_flag(det_lbl, LV_OBJ_FLAG_CLICKABLE);
  }

  return btn;
}

int eos_menu_get_focused_index(lv_obj_t *menu) {
  if (!menu) return -1;
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_obj_get_user_data(menu);
  if (!ctx || ctx->is_deleting) return -1;
  return ctx->focused_idx;
}


void eos_menu_focus_index(lv_obj_t *menu, int index) {
  if (!menu) return;
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_obj_get_user_data(menu);
  if (!ctx || ctx->is_deleting || !ctx->item_btns || ctx->cfg.item_count == 0) return;

  if (index < 0) index = 0;
  if ((size_t)index >= ctx->cfg.item_count) index = (int)ctx->cfg.item_count - 1;

  ctx->focused_idx = index;
  lv_group_t *grp = eos_lvgl_get_group();

  for (size_t i = 0; i < ctx->cfg.item_count; i++) {
    lv_obj_t *btn = ctx->item_btns[i];
    if (!btn) continue;
    lv_obj_t *txt_lbl = ctx->item_labels ? ctx->item_labels[i] : NULL;
    const eos_menu_item_t *item = ctx->items ? &ctx->items[i] : &ctx->cfg.items[i];

    if ((int)i == index) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PRIMARY_BLUE), 0);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(EOS_THEME_HIGHLIGHT_YELLOW), 0);
      lv_obj_set_style_border_width(btn, eos_lvgl_get_metrics().border_focus, 0);
      lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, 0);
      lv_obj_add_state(btn, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
      if (txt_lbl) {
        lv_obj_set_style_text_color(txt_lbl, lv_color_hex(EOS_THEME_TEXT_WHITE), 0);
      }
      lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
    } else {
      lv_obj_set_style_bg_color(btn, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
      lv_obj_set_style_border_width(btn, 1, 0);
      lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_clear_state(btn, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
      if (txt_lbl) {
        uint32_t text_col = (item && item->is_destructive) ? EOS_THEME_HIGHLIGHT_YELLOW : EOS_THEME_TEXT_PRIMARY;
        lv_obj_set_style_text_color(txt_lbl, lv_color_hex(text_col), 0);
      }
    }
  }

  if (ctx->cfg.on_focus) {
    const eos_menu_item_t *item = ctx->items ? &ctx->items[index] : &ctx->cfg.items[index];
    int item_id = item ? item->id : index;
    ctx->cfg.on_focus((eos_menu_t *)ctx, item_id, ctx->cfg.user_data);
  }
}


bool eos_menu_handle_key(lv_obj_t *menu, uint16_t key) {
  if (!menu) return false;
  eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_obj_get_user_data(menu);
  if (!ctx || ctx->is_deleting || ctx->cfg.item_count == 0) return false;

  switch (key) {
  case KEY_UP:
    if (ctx->focused_idx > 0) {
      eos_menu_focus_index(menu, ctx->focused_idx - 1);
    }
    return true;

  case KEY_DOWN:
    if ((size_t)ctx->focused_idx + 1 < ctx->cfg.item_count) {
      eos_menu_focus_index(menu, ctx->focused_idx + 1);
    }
    return true;

  case KEY_ENTER:
  case KEY_A: {
    int idx = ctx->focused_idx;
    if (idx >= 0 && (size_t)idx < ctx->cfg.item_count) {
      const eos_menu_item_t *item = ctx->items ? &ctx->items[idx] : &ctx->cfg.items[idx];
      if (item && !item->is_disabled) {
        if (item->action_cb) {
          item->action_cb((eos_menu_t *)ctx, item, ctx->cfg.user_data);
        } else if (ctx->cfg.on_select) {
          ctx->cfg.on_select((eos_menu_t *)ctx, item->id, ctx->cfg.user_data);
        }
        if (ctx->is_popup && ctx->root) {
          lv_obj_del_async(ctx->root);
        }
      }
    }
    return true;
  }

  case KEY_ESC:
  case KEY_B:
  case KEY_BACKSPACE:
    if (ctx->cfg.on_cancel) {
      ctx->cfg.on_cancel((eos_menu_t *)ctx, ctx->cfg.user_data);
    }
    if (ctx->is_popup && ctx->root) {
      lv_obj_del_async(ctx->root);
      return true;
    }
    return false;

  default:
    return false;
  }
}

lv_obj_t *eos_popup_menu_create(const eos_menu_cfg_t *cfg) {
  if (!cfg) return NULL;

  uint32_t sw = eos_lvgl_get_width();
  uint32_t sh = eos_lvgl_get_height();
  bool is_small = eos_screen_is_compact();
  lv_obj_t *parent = cfg->parent ? cfg->parent : lv_screen_active();

  eos_menu_ctx_t *ctx = calloc(1, sizeof(eos_menu_ctx_t));
  if (!ctx) return NULL;
  ctx->cfg = *cfg;
  ctx->is_popup = true;

  if (cfg->item_count > 0) {
    if (cfg->items) {
      ctx->items = calloc(cfg->item_count, sizeof(eos_menu_item_t));
      if (ctx->items) {
        memcpy(ctx->items, cfg->items, cfg->item_count * sizeof(eos_menu_item_t));
        ctx->cfg.items = ctx->items;
      }
    }
    ctx->item_btns = calloc(cfg->item_count, sizeof(lv_obj_t *));
    ctx->item_labels = calloc(cfg->item_count, sizeof(lv_obj_t *));
  }

  // Translucent backdrop
  lv_obj_t *ov = lv_obj_create(parent);
  ctx->root = ov;
  lv_obj_set_user_data(ov, ctx);
  lv_obj_set_size(ov, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_align(ov, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(ov, (lv_opa_t)EOS_MENU_BACKDROP_OPACITY, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_pad_all(ov, 0, 0);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ov, eos_menu_backdrop_click_cb, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(ov, eos_menu_context_cleanup_cb, LV_EVENT_DELETE, ctx);

  int32_t card_w = (int32_t)sw;
  int32_t card_h = (int32_t)sh;

  bool show_hdr = (!cfg->headerless && cfg->title);
  int32_t hdr_h = show_hdr ? EOS_MENU_HEADER_HEIGHT : 0;
  int32_t item_h = is_small ? EOS_MENU_ITEM_HEIGHT_SMALL : EOS_MENU_ITEM_HEIGHT_LARGE;

  lv_obj_t *card = lv_obj_create(ov);
  ctx->card = card;
  lv_obj_set_size(card, lv_pct(EOS_PERCENT_MAX), lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(EOS_THEME_PANEL_COLOR), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_clip_corner(card, true, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Optional header banner
  if (show_hdr) {
    lv_obj_t *hdr = lv_obj_create(card);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, card_w, EOS_MENU_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, eos_lvgl_get_metrics().pad_lg, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(title_lbl, eos_lvgl_get_font(), 0);
    if (cfg->icon) {
      lv_label_set_text_fmt(title_lbl, "%s  %s", cfg->icon, cfg->title);
    } else {
      lv_label_set_text(title_lbl, cfg->title);
    }
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
    lv_obj_center(title_lbl);
  }

  // Linear button items (no gaps, 0px spacing, linear coordinate stacking)
  for (size_t i = 0; i < cfg->item_count; i++) {
    const eos_menu_item_t *item = ctx->items ? &ctx->items[i] : &cfg->items[i];
    lv_obj_t *lbl = NULL;
    lv_obj_t *btn = eos_menu_render_item_button(card, item, ctx, (int32_t)(hdr_h + i * item_h),
                                                card_w, item_h, 10, 26, &lbl);
    lv_obj_set_width(btn, lv_pct(EOS_PERCENT_MAX));
    if (ctx->item_btns) ctx->item_btns[i] = btn;
    if (ctx->item_labels) ctx->item_labels[i] = lbl;
  }

  if (cfg->item_count > 0) {
    eos_menu_focus_index(ov, 0);
  }

  return ov;
}

lv_obj_t *eos_menu_create(const eos_menu_cfg_t *cfg) {
  if (!cfg) return NULL;

  uint32_t sw = eos_lvgl_get_width();
  uint32_t sh = eos_lvgl_get_height();
  bool is_small = eos_screen_is_compact();
  lv_obj_t *parent = cfg->parent ? cfg->parent : lv_screen_active();

  eos_menu_ctx_t *ctx = calloc(1, sizeof(eos_menu_ctx_t));
  if (!ctx) return NULL;
  ctx->cfg = *cfg;
  ctx->is_popup = false;

  if (cfg->item_count > 0) {
    if (cfg->items) {
      ctx->items = calloc(cfg->item_count, sizeof(eos_menu_item_t));
      if (ctx->items) {
        memcpy(ctx->items, cfg->items, cfg->item_count * sizeof(eos_menu_item_t));
        ctx->cfg.items = ctx->items;
      }
    }
    ctx->item_btns = calloc(cfg->item_count, sizeof(lv_obj_t *));
    ctx->item_labels = calloc(cfg->item_count, sizeof(lv_obj_t *));
  }

  int32_t total_w = (is_small || cfg->width <= 0) ? (int32_t)sw : cfg->width;
  int32_t total_h = cfg->height > 0 ? cfg->height : (int32_t)sh;

  lv_obj_t *cont = lv_obj_create(parent);
  ctx->root = cont;
  lv_obj_set_user_data(cont, ctx);
  if (is_small || cfg->width <= 0) {
    lv_obj_set_width(cont, lv_pct(EOS_PERCENT_MAX));
  } else {
    lv_obj_set_width(cont, total_w);
  }
  lv_obj_set_height(cont, total_h);
  lv_obj_set_pos(cont, 0, 0);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(cont, 0, 0);
  lv_obj_set_style_bg_color(cont, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_hor(cont, 0, 0);
  lv_obj_set_style_pad_ver(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(cont, eos_menu_context_cleanup_cb, LV_EVENT_DELETE, ctx);

  bool show_header = !cfg->headerless && cfg->title;
  if (show_header) {
    lv_obj_t *hdr = lv_obj_create(cont);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, lv_pct(EOS_PERCENT_MAX), EOS_MENU_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(EOS_THEME_SURFACE_COLOR), 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(EOS_THEME_BORDER_COLOR), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, eos_lvgl_get_metrics().pad_md, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(title_lbl, eos_lvgl_get_font(), 0);
    if (cfg->icon) {
      lv_label_set_text_fmt(title_lbl, "%s  %s", cfg->icon, cfg->title);
    } else {
      lv_label_set_text(title_lbl, cfg->title);
    }
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(EOS_THEME_TEXT_PRIMARY), 0);
    lv_obj_center(title_lbl);
  }

  // Scrollable list container (linear coordinate stacking, no heavy flex)
  int32_t hdr_h = show_header ? EOS_MENU_HEADER_HEIGHT : 0;
  int32_t list_h = total_h - hdr_h;
  lv_obj_t *list = lv_obj_create(cont);
  lv_obj_set_pos(list, 0, hdr_h);
  lv_obj_set_width(list, lv_pct(EOS_PERCENT_MAX));
  lv_obj_set_height(list, list_h);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_style_bg_color(list, lv_color_hex(EOS_THEME_BG_COLOR), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_hor(list, 0, 0);
  lv_obj_set_style_pad_ver(list, 0, 0);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  int32_t item_h = is_small ? EOS_MENU_ITEM_HEIGHT_SMALL : EOS_MENU_ITEM_HEIGHT_LARGE;

  for (size_t i = 0; i < cfg->item_count; i++) {
    const eos_menu_item_t *item = ctx->items ? &ctx->items[i] : &cfg->items[i];
    lv_obj_t *lbl = NULL;
    lv_obj_t *btn = eos_menu_render_item_button(list, item, ctx, (int32_t)(i * item_h),
                                                total_w, item_h, 8, 24, &lbl);
    if (is_small || cfg->width <= 0) {
      lv_obj_set_width(btn, lv_pct(EOS_PERCENT_MAX));
    }
    if (ctx->item_btns) ctx->item_btns[i] = btn;
    if (ctx->item_labels) ctx->item_labels[i] = lbl;
  }

  if (cfg->item_count > 0) {
    eos_menu_focus_index(cont, 0);
  }

  return cont;
}

lv_obj_t *eos_open_menu(const eos_menu_cfg_t *cfg) {
  if (!cfg) return NULL;
  eos_menu_cfg_t menu_cfg = *cfg;
  if (eos_screen_is_compact()) {
    menu_cfg.headerless = true; // Headerless mode on small screens
  }
  menu_cfg.width = 0;  // Full available area by width on all screens
  menu_cfg.height = 0; // Full available area by height on all screens
  return eos_menu_create(&menu_cfg);
}

void eos_menu_close(lv_obj_t *menu) {
  if (menu) {
    eos_menu_ctx_t *ctx = (eos_menu_ctx_t *)lv_obj_get_user_data(menu);
    if (ctx) {
      ctx->is_deleting = true;
    }
    lv_obj_del_async(menu);
  }
}
