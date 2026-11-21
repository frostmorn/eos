#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ecore/lvgl.h"
#include "emisc/strlimits.h"
#include "lvgl_backlight.h"

typedef struct {
  int cur_brightness;
  bool smooth_fade;
  bool quit;
  int dev_fd;
  bool is_pwm;

  lv_obj_t *lbl_title;
  lv_obj_t *slider;
  lv_obj_t *lbl_val;
  lv_obj_t *btn_presets[EOS_BACKLIGHT_PRESET_COUNT];
  lv_obj_t *sw_fade;
  lv_obj_t *btn_exit;
} backlight_state_t;

static const int s_presets[EOS_BACKLIGHT_PRESET_COUNT] = {10, 25, 50, 75, 100};

static void backlight_open_device(backlight_state_t *st) {
  st->dev_fd = -1;
  st->is_pwm = false;
  st->cur_brightness = EOS_BACKLIGHT_MAX_DUTY;

  const char *pwm_env = getenv("EOS_PWM");
  const char *pwm_path = pwm_env ? pwm_env : "/dev/pwm0";

  int fd = open(pwm_path, O_RDWR);
  if (fd >= 0) {
    st->dev_fd = fd;
    st->is_pwm = true;
    char buf[EOS_STR_10_LEN] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      int val = atoi(buf);
      if (val >= 0 && val <= EOS_BACKLIGHT_MAX_DUTY) {
        st->cur_brightness = val;
      }
    }
    return;
  }

  const char *disp_env = getenv("EOS_DISPLAY");
  const char *disp_path = disp_env ? disp_env : "/dev/fb0";
  fd = open(disp_path, O_RDWR);
  if (fd >= 0) {
    st->dev_fd = fd;
    st->is_pwm = false;
    uint8_t b = 100;
    if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_BRIGHTNESS, &b) == 0) {
      st->cur_brightness = (int)b;
    }
  }
}

static void backlight_apply_hardware(backlight_state_t *st, int val) {
  if (val < 0) val = 0;
  if (val > EOS_BACKLIGHT_MAX_DUTY) val = EOS_BACKLIGHT_MAX_DUTY;
  st->cur_brightness = val;

  if (st->dev_fd < 0) return;

  if (st->is_pwm) {
    if (st->smooth_fade) {
      eos_pwm_fade_t fade = {
          .target_duty = (uint16_t)(((uint32_t)val * 65535U) / 100U),
          .duration_ms = EOS_BACKLIGHT_FADE_MS,
      };
      ioctl(st->dev_fd, EOS_PWM_IOCTL_FADE, &fade);
    } else {
      ioctl(st->dev_fd, EOS_PWM_IOCTL_SET_DUTY, (uint32_t)val);
    }
  } else {
    ioctl(st->dev_fd, EOS_DISPLAY_IOCTL_SET_BRIGHTNESS, (uint32_t)val);
  }
}

static void backlight_update_ui(backlight_state_t *st) {
  if (st->slider) {
    lv_slider_set_value(st->slider, st->cur_brightness, LV_ANIM_OFF);
  }
  if (st->lbl_val) {
    char buf[EOS_SMALL_STR_LEN];
    snprintf(buf, sizeof(buf), "Brightness: %d%%", st->cur_brightness);
    lv_label_set_text(st->lbl_val, buf);
  }
}

static void slider_event_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (!st || !st->slider) return;
  int val = lv_slider_get_value(st->slider);
  backlight_apply_hardware(st, val);
  backlight_update_ui(st);
}

static void preset_event_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (!st) return;
  lv_obj_t *btn = lv_event_get_target(e);
  for (int i = 0; i < EOS_BACKLIGHT_PRESET_COUNT; i++) {
    if (st->btn_presets[i] == btn) {
      backlight_apply_hardware(st, s_presets[i]);
      backlight_update_ui(st);
      break;
    }
  }
}

static void fade_sw_event_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (!st || !st->sw_fade) return;
  st->smooth_fade = lv_obj_has_state(st->sw_fade, LV_STATE_CHECKED);
}

static void exit_btn_cb(lv_event_t *e) {
  backlight_state_t *st = (backlight_state_t *)lv_event_get_user_data(e);
  if (st) st->quit = true;
}

static void backlight_setup_ui(backlight_state_t *st, uint32_t w, uint32_t h) {
  (void)h;
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);

  st->lbl_title = lv_label_create(scr);
  lv_label_set_text(st->lbl_title, "Screen Backlight");
  lv_obj_align(st->lbl_title, LV_ALIGN_TOP_MID, 0, 10);

  st->lbl_val = lv_label_create(scr);
  lv_obj_align(st->lbl_val, LV_ALIGN_TOP_MID, 0, 36);

  st->slider = lv_slider_create(scr);
  lv_slider_set_range(st->slider, 0, EOS_BACKLIGHT_MAX_DUTY);
  lv_obj_set_width(st->slider, (lv_coord_t)(w - 40));
  lv_obj_align(st->slider, LV_ALIGN_TOP_MID, 0, 68);
  lv_obj_add_event_cb(st->slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, st);

  int btn_w = (int)((w - 40) / EOS_BACKLIGHT_PRESET_COUNT) - 4;
  if (btn_w < 28) btn_w = 28;
  int start_x = -(int)((EOS_BACKLIGHT_PRESET_COUNT * (btn_w + 4)) / 2) + (btn_w / 2);

  for (int i = 0; i < EOS_BACKLIGHT_PRESET_COUNT; i++) {
    st->btn_presets[i] = lv_btn_create(scr);
    lv_obj_set_size(st->btn_presets[i], btn_w, 28);
    lv_obj_align(st->btn_presets[i], LV_ALIGN_TOP_MID, start_x + i * (btn_w + 4), 104);
    lv_obj_add_event_cb(st->btn_presets[i], preset_event_cb, LV_EVENT_CLICKED, st);

    lv_obj_t *lbl = lv_label_create(st->btn_presets[i]);
    char pbuf[EOS_STR_8_LEN];
    snprintf(pbuf, sizeof(pbuf), "%d%%", s_presets[i]);
    lv_label_set_text(lbl, pbuf);
    lv_obj_center(lbl);
  }

  lv_obj_t *lbl_fade = lv_label_create(scr);
  lv_label_set_text(lbl_fade, "Smooth Fade:");
  lv_obj_align(lbl_fade, LV_ALIGN_TOP_MID, -36, 148);

  st->sw_fade = lv_switch_create(scr);
  lv_obj_align(st->sw_fade, LV_ALIGN_TOP_MID, 40, 144);
  if (st->smooth_fade) {
    lv_obj_add_state(st->sw_fade, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(st->sw_fade, fade_sw_event_cb, LV_EVENT_VALUE_CHANGED, st);

  st->btn_exit = lv_btn_create(scr);
  lv_obj_set_size(st->btn_exit, 90, 32);
  lv_obj_align(st->btn_exit, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(st->btn_exit, exit_btn_cb, LV_EVENT_CLICKED, st);

  lv_obj_t *lbl_exit = lv_label_create(st->btn_exit);
  lv_label_set_text(lbl_exit, "Done");
  lv_obj_center(lbl_exit);
}

static void backlight_handle_key(backlight_state_t *st, uint16_t key) {
  switch (key) {
  case KEY_LEFT:
  case KEY_DOWN:
    backlight_apply_hardware(st, st->cur_brightness - EOS_BACKLIGHT_DEFAULT_STEP);
    backlight_update_ui(st);
    break;

  case KEY_RIGHT:
  case KEY_UP:
    backlight_apply_hardware(st, st->cur_brightness + EOS_BACKLIGHT_DEFAULT_STEP);
    backlight_update_ui(st);
    break;

  case KEY_BACKSPACE:
  case KEY_B:
  case KEY_ESC:
    st->quit = true;
    break;

  default:
    break;
  }
}

int lvgl_backlight_main(int argc, char **argv) {
  const char *fb_path = (argc > 1) ? argv[1] : NULL;
  const char *kbd_path = (argc > 2) ? argv[2] : NULL;

  if (eos_lvgl_init_paths(fb_path, kbd_path) != 0) {
    printf("lvgl_backlight: failed to initialize LVGL\n");
    return -1;
  }

  backlight_state_t st;
  memset(&st, 0, sizeof(st));
  st.smooth_fade = true;

  backlight_open_device(&st);

  uint32_t w = eos_lvgl_get_width();
  uint32_t h = eos_lvgl_get_height();

  backlight_setup_ui(&st, w, h);
  backlight_update_ui(&st);

  FILE *tb = fopen("/tmp/tbar.tmp", "w");
  if (tb) {
    fprintf(tb, "@%ld Backlight\n[◄►]Brightness  [B]Exit\n", (long)getpid());
    fclose(tb);
    rename("/tmp/tbar.tmp", "/tmp/tbar");
  }

  while (!st.quit) {
    uint16_t key = 0;
    while (eos_lvgl_poll_key(&key)) {
      backlight_handle_key(&st, key);
    }
    eos_lvgl_step(10);
  }

  if (st.dev_fd >= 0) {
    close(st.dev_fd);
    st.dev_fd = -1;
  }

  eos_lvgl_deinit();
  return 0;
}

EOS_BIN_ATTR const eos_bin_t lvgl_backlight_app = {
    EOS_BIN_INITIALIZER,
    .flags       = EOS_BIN_FLAG_GUI,
    .stack_size  = EOS_BIN_GUI_STACK_SIZE,
    .filename    = "lvgl_backlight",
    .name        = "Backlight",
    .group       = "Utilities",
    .description = "Adjust screen backlight brightness and smooth fade",
    .entry_point = lvgl_backlight_main,
};
