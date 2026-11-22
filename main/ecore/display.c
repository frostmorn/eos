#include "ecore/display.h"
#include "ecore/dev.h"
#include "ecore/log.h"
#include "ecore/proc.h"
#include "emisc/strlimits.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  pid_t owner_pid;
  uint32_t orig_fmt;
  uint32_t active_fmt;
  char fb_path[EOS_MID_STR_LEN];
  SemaphoreHandle_t lock;
} eos_disp_state_t;

static eos_disp_state_t s_disp = {
    .owner_pid = 0,
    .orig_fmt = EOS_DISPLAY_FMT_RGB565,
    .active_fmt = EOS_DISPLAY_FMT_RGB565,
    .fb_path = "",
    .lock = NULL,
};

static void eos_display_ensure_lock(void) {
  if (!s_disp.lock) {
    s_disp.lock = xSemaphoreCreateRecursiveMutex();
  }
}

static inline void eos_disp_lock(void) {
  eos_display_ensure_lock();
  if (s_disp.lock) {
    xSemaphoreTakeRecursive(s_disp.lock, portMAX_DELAY);
  }
}

static inline void eos_disp_unlock(void) {
  if (s_disp.lock) {
    xSemaphoreGiveRecursive(s_disp.lock);
  }
}

static const char *eos_display_resolve_path(pid_t pid) {
  const char *fb = eos_proc_getenv_pid(pid, "EOS_DISPLAY");
  if (!fb || !fb[0]) {
    fb = eos_proc_getenv_pid(pid, "EOS_FB");
  }
  if (!fb || !fb[0]) {
    fb = "/dev/fb0";
  }
  return fb;
}

static void eos_display_format_cleanup_locked(void) {
  if (s_disp.owner_pid > 0 && s_disp.fb_path[0] != '\0') {
    int fd = open(s_disp.fb_path, O_RDWR);
    if (fd >= 0) {
      (void)ioctl(fd, EOS_DISPLAY_IOCTL_SET_FORMAT, s_disp.orig_fmt);
      close(fd);
      EOS_LOGI("display: restored '%s' to format %u on PID %d exit",
               s_disp.fb_path, (unsigned int)s_disp.orig_fmt, (int)s_disp.owner_pid);
    }
    s_disp.owner_pid = 0;
    s_disp.active_fmt = s_disp.orig_fmt;
    s_disp.fb_path[0] = '\0';
  }
}

static void eos_display_format_cleanup_hook(void *arg) {
  (void)arg;
  eos_disp_lock();
  eos_display_format_cleanup_locked();
  eos_disp_unlock();
}

int eos_display_apply_format_pid(pid_t pid, uint32_t req_fmt) {
  if (req_fmt == EOS_DISPLAY_FMT_UNSPECIFIED) {
    return 0;
  }

  eos_disp_lock();

  const char *fb_path = eos_display_resolve_path(pid);
  int fd = open(fb_path, O_RDWR);
  if (fd < 0) {
    EOS_LOGE("display: cannot open '%s' for format negotiation: %s", fb_path, strerror(errno));
    eos_disp_unlock();
    return -1;
  }

  uint32_t orig_fmt = EOS_DISPLAY_FMT_RGB565;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMAT, &orig_fmt) != 0) {
    orig_fmt = EOS_DISPLAY_FMT_RGB565;
  }

  if (orig_fmt == req_fmt) {
    close(fd);
    eos_disp_unlock();
    return 0;
  }

  // Enforce single GUI format owner policy
  if (s_disp.owner_pid > 0 && s_disp.owner_pid != pid && s_disp.active_fmt != req_fmt) {
    EOS_LOGW("display: '%s' format busy (held by PID %d), rejecting PID %d",
             fb_path, (int)s_disp.owner_pid, (int)pid);
    close(fd);
    eos_disp_unlock();
    errno = EBUSY;
    return -1;
  }

  uint32_t supported = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMATS, &supported) != 0 ||
      !(supported & EOS_DISPLAY_FMT_BIT(req_fmt))) {
    EOS_LOGE("display: format %u not supported by '%s' (supported mask: 0x%08x)",
             (unsigned int)req_fmt, fb_path, (unsigned int)supported);
    close(fd);
    eos_disp_unlock();
    errno = ENOTSUP;
    return -1;
  }

  if (ioctl(fd, EOS_DISPLAY_IOCTL_SET_FORMAT, req_fmt) != 0) {
    EOS_LOGE("display: failed to set format %u on '%s': %s",
             (unsigned int)req_fmt, fb_path, strerror(errno));
    close(fd);
    eos_disp_unlock();
    return -1;
  }

  close(fd);

  s_disp.owner_pid = pid;
  s_disp.orig_fmt = orig_fmt;
  s_disp.active_fmt = req_fmt;
  strlcpy(s_disp.fb_path, fb_path, sizeof(s_disp.fb_path));

  eos_proc_on_exit_pid(pid, eos_display_format_cleanup_hook, NULL, "display_format_restore");

  EOS_LOGI("display: PID %d claimed '%s' with format %u (original %u)",
           (int)pid, fb_path, (unsigned int)req_fmt, (unsigned int)orig_fmt);

  eos_disp_unlock();
  return 0;
}

void eos_display_release_format_pid(pid_t pid) {
  eos_disp_lock();
  if (s_disp.owner_pid == pid) {
    eos_display_format_cleanup_locked();
  }
  eos_disp_unlock();
  eos_proc_cancel_exit_match(pid, eos_display_format_cleanup_hook, NULL);
}

uint32_t eos_display_get_active_format(const char *fb_path) {
  const char *path = (fb_path && fb_path[0]) ? fb_path : "/dev/fb0";
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return EOS_DISPLAY_FMT_RGB565;
  }
  uint32_t fmt = EOS_DISPLAY_FMT_RGB565;
  (void)ioctl(fd, EOS_DISPLAY_IOCTL_GET_FORMAT, &fmt);
  close(fd);
  return fmt;
}

pid_t eos_display_get_owner_pid(void) {
  return s_disp.owner_pid;
}

const char *eos_display_default_fb_path(void) {
  const char *env = getenv("EOS_FB");
  if (env && env[0] != '\0') {
    return env;
  }
  if (access("/dev/vfb3", F_OK) == 0) return "/dev/vfb3";
  if (access("/dev/fb0", F_OK) == 0) return "/dev/fb0";
  if (access("/dev/vfb0", F_OK) == 0) return "/dev/vfb0";
  return "/dev/fb0";
}

const char *eos_display_role_fb_path(const char *role) {
  const char *env = getenv("EOS_FB");
  if (env && env[0] != '\0') {
    return env;
  }
  if (role) {
    if (strcmp(role, "statusbar") == 0) {
      return (access("/dev/vfb1", F_OK) == 0) ? "/dev/vfb1" : "/dev/fb0";
    }
    if (strcmp(role, "toolbar") == 0) {
      return (access("/dev/vfb2", F_OK) == 0) ? "/dev/vfb2" : "/dev/fb0";
    }
  }
  return eos_display_default_fb_path();
}
