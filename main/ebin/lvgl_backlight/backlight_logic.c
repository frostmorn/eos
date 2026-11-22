#include "backlight_logic.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ecore/ioctl.h"

static const int s_presets[EOS_BACKLIGHT_PRESET_COUNT] = {10, 25, 50, 75, 100};

const int *backlight_logic_get_presets(size_t *count) {
  if (count) *count = EOS_BACKLIGHT_PRESET_COUNT;
  return s_presets;
}

int backlight_logic_init(backlight_device_t *dev) {
  if (!dev) return -1;
  dev->dev_fd = -1;
  dev->is_pwm = false;
  dev->cur_brightness = EOS_BACKLIGHT_MAX_DUTY;
  dev->smooth_fade = false;

  const char *pwm_env = getenv("EOS_PWM");
  const char *pwm_path = pwm_env ? pwm_env : "/dev/pwm0";

  int fd = open(pwm_path, O_RDWR);
  if (fd >= 0) {
    dev->dev_fd = fd;
    dev->is_pwm = true;
    char buf[EOS_NUM_STR_LEN] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      int val = atoi(buf);
      if (val >= 0 && val <= EOS_BACKLIGHT_MAX_DUTY) {
        dev->cur_brightness = val;
      }
    }
    return 0;
  }

  const char *disp_env = getenv("EOS_FB");
  if (!disp_env) disp_env = getenv("EOS_DISPLAY");
  const char *disp_path = disp_env ? disp_env : EOS_BACKLIGHT_DEFAULT_DEV;
  fd = open(disp_path, O_RDWR);
  if (fd >= 0) {
    dev->dev_fd = fd;
    dev->is_pwm = false;
    uint8_t b = 100;
    if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_BRIGHTNESS, &b) == 0) {
      dev->cur_brightness = (int)b;
    }
    return 0;
  }

  return -1;
}

void backlight_logic_close(backlight_device_t *dev) {
  if (dev && dev->dev_fd >= 0) {
    close(dev->dev_fd);
    dev->dev_fd = -1;
  }
}

int backlight_logic_apply(backlight_device_t *dev, int val) {
  if (!dev) return -1;
  if (val < 0) val = 0;
  if (val > EOS_BACKLIGHT_MAX_DUTY) val = EOS_BACKLIGHT_MAX_DUTY;
  dev->cur_brightness = val;

  if (dev->dev_fd < 0) return 0;

  if (dev->is_pwm) {
    if (dev->smooth_fade) {
      eos_pwm_fade_t fade = {
          .target_duty = (uint16_t)(((uint32_t)val * 65535U) / 100U),
          .duration_ms = EOS_BACKLIGHT_FADE_MS,
      };
      return ioctl(dev->dev_fd, EOS_PWM_IOCTL_FADE, &fade);
    }
    return ioctl(dev->dev_fd, EOS_PWM_IOCTL_SET_DUTY, (uint32_t)val);
  }

  return ioctl(dev->dev_fd, EOS_DISPLAY_IOCTL_SET_BRIGHTNESS, (uint32_t)val);
}

int backlight_logic_step(backlight_device_t *dev, int delta) {
  if (!dev) return -1;
  return backlight_logic_apply(dev, dev->cur_brightness + delta);
}

void backlight_logic_format_label(int brightness, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  snprintf(buf, buflen, "Brightness: %d%%", brightness);
}
