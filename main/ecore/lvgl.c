#include "ecore/lvgl.h"
#include "emisc/fancymacro.h"

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

// Maximum DMA chunk size matching st7789 driver strip fallback (16 KB)
#define EOS_LVGL_DMA_STRIP_BYTES 16384U
#define EOS_LVGL_KEY_QUEUE_SIZE  32U

typedef struct {
  eos_lvgl_ctx_t pub;
  uint16_t key_queue[EOS_LVGL_KEY_QUEUE_SIZE];
  uint8_t key_head;
  uint8_t key_tail;
  uint32_t pending_lv_key;
  bool send_release;
} eos_lvgl_internal_t;

static eos_lvgl_internal_t s_lvgl = {
    .pub =
        {
            .initialized = false,
            .fb_fd = -1,
            .kbd_fd = -1,
        },
};

static bool s_lv_core_initialized = false;

// ── Monotonic Millisecond Tick Callback ────────────────────────

static uint32_t eos_lvgl_tick_cb(void) {
  static uint32_t fallback_tick = 0;
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0) {
    uint32_t ms =
        (uint32_t)((uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL);
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
  uint8_t next = (uint8_t)((s_lvgl.key_head + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  if (next == s_lvgl.key_tail) {
    // Drop oldest key on overflow
    s_lvgl.key_tail = (uint8_t)((s_lvgl.key_tail + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  }
  s_lvgl.key_queue[s_lvgl.key_head] = eos_key;
  s_lvgl.key_head = next;
}

static void poll_hardware_keyboard(void) {
  if (s_lvgl.pub.kbd_fd < 0)
    return;

  for (int i = 0; i < 8; i++) {
    eos_input_event_t ev;
    memset(&ev, 0, sizeof(ev));
    int ret = ioctl(s_lvgl.pub.kbd_fd, EOS_KEYBOARD_IOCTL_POLL, &ev);
    if (ret > 0 && ev.type == EOS_INPUT_EVENT_KEYBOARD &&
        ev.code != KEY_RESERVED) {
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
  case KEY_B:
    return LV_KEY_ESC;
  case KEY_C:
    return LV_KEY_PREV;
  case KEY_D:
    return LV_KEY_NEXT;
  case KEY_SPACE:
    return ' ';
  case KEY_BACKSPACE:
    return LV_KEY_BACKSPACE;
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
    data->continue_reading = (s_lvgl.key_head != s_lvgl.key_tail);
    return;
  }

  if (s_lvgl.pub.kbd_fd < 0) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  eos_input_event_t ev;
  memset(&ev, 0, sizeof(ev));
  int ret = ioctl(s_lvgl.pub.kbd_fd, EOS_KEYBOARD_IOCTL_POLL, &ev);
  if (ret > 0 && ev.type == EOS_INPUT_EVENT_KEYBOARD &&
      ev.code != KEY_RESERVED) {
    key_queue_push(ev.code);
    bool group_nav = s_lvgl.pub.dpad_group_nav;
    if (group_nav && s_lvgl.pub.group &&
        lv_group_get_editing(s_lvgl.pub.group)) {
      group_nav = false;
    }
    uint32_t lv_key = eos_lvgl_map_key(ev.code, group_nav);
    if (s_lvgl.pub.key_cb) {
      s_lvgl.pub.key_cb(ev.code, lv_key, s_lvgl.pub.key_cb_user_data);
    }
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

  size_t row_bytes = (size_t)ctx->width * sizeof(uint16_t);
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
  size_t full_bytes = (size_t)ctx->width * ctx->height * sizeof(uint16_t);
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
  uint32_t src_stride_bytes =
      lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_RGB565);
  if (src_stride_bytes < (uint32_t)w * sizeof(uint16_t)) {
    src_stride_bytes = (uint32_t)w * sizeof(uint16_t);
  }

  const uint8_t *src_bytes = (const uint8_t *)src;

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

    const uint16_t *src_row =
        (const uint16_t *)(src_bytes + (size_t)row * src_stride_bytes);
    memcpy(&ctx->full_buf[(size_t)dst_y * ctx->width + (size_t)dst_x],
           &src_row[src_x], (size_t)copy_w * sizeof(uint16_t));
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
        size_t chunk_bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
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

int eos_lvgl_init(const eos_lvgl_cfg_t *cfg) {
  if (s_lvgl.pub.initialized) {
    eos_lvgl_deinit();
  }

  memset(&s_lvgl, 0, sizeof(s_lvgl));
  s_lvgl.pub.fb_fd = -1;
  s_lvgl.pub.kbd_fd = -1;

  const char *fb_path = (cfg && cfg->fb_path) ? cfg->fb_path : getenv("EOS_FB");
  if (!fb_path || !fb_path[0]) {
    fb_path = EOS_LVGL_DEFAULT_FB_PATH;
  }

  const char *kbd_path =
      (cfg && cfg->kbd_path) ? cfg->kbd_path : getenv("EOS_KEYBOARD");
  if (!kbd_path || !kbd_path[0]) {
    kbd_path = EOS_LVGL_DEFAULT_KBD_PATH;
  }

  bool enable_kbd = cfg ? cfg->enable_keyboard : true;
  s_lvgl.pub.dpad_group_nav = cfg ? cfg->dpad_group_nav : true;

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

  // Compute partial strip height modeled after st7789.c (16 KB DMA strip)
  size_t row_bytes = (size_t)width * sizeof(uint16_t);
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
  s_lvgl.pub.full_buf_size = (size_t)width * (size_t)height * sizeof(uint16_t);

  // Allocate shadow full-frame buffer in PSRAM if available to preserve internal DMA SRAM
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

  // Allocate partial draw buffer (preferring PSRAM/default heap so 32KB DMA pool stays free for st7789)
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

  // Verify whether display driver supports EOS_DISPLAY_IOCTL_SET_WINDOW
  eos_display_rect_t probe_rect = {
      .x = 0, .y = 0, .w = width, .h = height};
  if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &probe_rect) != 0) {
    EOS_LOGW("Display driver rejected SET_WINDOW; enabling full-frame fallback");
    s_lvgl.pub.fallback_full = true;
  } else {
    (void)ioctl(fb_fd, EOS_DISPLAY_IOCTL_SET_WINDOW, NULL);
  }

  // If partial buffer allocation failed or full frame was requested, fall back to full buffer
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

  if (!s_lv_core_initialized) {
    lv_init();
    lv_tick_set_cb(eos_lvgl_tick_cb);
    s_lv_core_initialized = true;
  }

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

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

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

  // Create default focus group
  s_lvgl.pub.group = lv_group_create();
  if (s_lvgl.pub.group) {
    lv_group_set_default(s_lvgl.pub.group);
  }

  // Open keyboard device if enabled
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

void eos_lvgl_deinit(void) {
  if (!s_lvgl.pub.initialized)
    return;

  if (s_lvgl.pub.display) {
    lv_obj_t *scr = lv_screen_active();
    if (scr) {
      lv_obj_clean(scr);
    }
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

  s_lvgl.pub.initialized = false;
}

eos_lvgl_ctx_t *eos_lvgl_get_ctx(void) {
  return s_lvgl.pub.initialized ? &s_lvgl.pub : NULL;
}

lv_display_t *eos_lvgl_get_display(void) { return s_lvgl.pub.display; }

lv_indev_t *eos_lvgl_get_indev(void) { return s_lvgl.pub.indev; }

lv_group_t *eos_lvgl_get_group(void) { return s_lvgl.pub.group; }

uint32_t eos_lvgl_get_width(void) { return s_lvgl.pub.width; }

uint32_t eos_lvgl_get_height(void) { return s_lvgl.pub.height; }

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

  usleep(delay * 1000U);
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

  if (s_lvgl.key_head == s_lvgl.key_tail)
    return false;

  uint16_t key = s_lvgl.key_queue[s_lvgl.key_tail];
  s_lvgl.key_tail = (uint8_t)((s_lvgl.key_tail + 1U) % EOS_LVGL_KEY_QUEUE_SIZE);
  if (eos_key_out) {
    *eos_key_out = key;
  }
  return true;
}

void eos_lvgl_clear_keys(void) {
  s_lvgl.key_head = 0;
  s_lvgl.key_tail = 0;
  s_lvgl.send_release = false;
}
