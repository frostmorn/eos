#include "ecore/log.h"
#include "ecore/fapi.h"
#include "ecore/threadctx.h"
#include "emisc/strlimits.h"

#include <esp_log.h>
#include <fcntl.h>
#include <reent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#if !defined(__has_include) || __has_include(<driver/usb_serial_jtag.h>)
#include <driver/usb_serial_jtag.h>
#define EOS_HAS_USJ_DRIVER 1
#endif
#endif

extern int __real__open_r(struct _reent *r, char *path, int flags, int mode);
extern int __real__close_r(struct _reent *r, int fd);
extern ssize_t __real__write_r(struct _reent *r, int fd, const void *data,
                               size_t size);
extern int __real__fstat_r(struct _reent *r, int fd, struct stat *st);

static vprintf_like_t s_prev_vprintf = NULL;
static bool s_log_initialized = false;
static __thread bool s_in_log_hook = false;

// Global synced state for EOS_LOG and EOS_LOG_FILE across all threads
static bool s_global_log_enabled = false;
static bool s_global_log_explicit_off = false;
static char s_global_log_path[EOS_MID_STR_LEN] = EOS_DEFAULT_LOG_FILE;

static bool is_truthy_str(const char *val) {
  if (!val || !val[0])
    return false;
  return (strcasecmp(val, "1") == 0 || strcasecmp(val, "on") == 0 ||
          strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0);
}

static bool is_falsy_str(const char *val) {
  if (!val || !val[0])
    return true;
  return (strcasecmp(val, "0") == 0 || strcasecmp(val, "off") == 0 ||
          strcasecmp(val, "false") == 0 || strcasecmp(val, "no") == 0 ||
          strcasecmp(val, "none") == 0);
}

bool eos_console_is_connected(void) {
#ifdef EOS_HAS_USJ_DRIVER
  return usb_serial_jtag_is_connected();
#else
  return true;
#endif
}

void eos_log_sync_env(const char *name, const char *value) {
  if (!name)
    return;

  if (strcmp(name, "EOS_LOG") == 0) {
    if (!value || is_falsy_str(value)) {
      s_global_log_enabled = false;
      s_global_log_explicit_off = (value != NULL && value[0] != '\0');
    } else if (is_truthy_str(value)) {
      s_global_log_enabled = true;
      s_global_log_explicit_off = false;
      if (!s_global_log_path[0]) {
        strlcpy(s_global_log_path, EOS_DEFAULT_LOG_FILE,
                sizeof(s_global_log_path));
      }
    } else {
      // Allow EOS_LOG=/path/to/file.log as a shorthand
      s_global_log_enabled = true;
      s_global_log_explicit_off = false;
      strlcpy(s_global_log_path, value, sizeof(s_global_log_path));
    }
  } else if (strcmp(name, "EOS_LOG_FILE") == 0 ||
             strcmp(name, "LOG_FILE") == 0) {
    if (!value || is_falsy_str(value)) {
      s_global_log_enabled = false;
    } else if (is_truthy_str(value)) {
      strlcpy(s_global_log_path, EOS_DEFAULT_LOG_FILE,
              sizeof(s_global_log_path));
      if (!s_global_log_explicit_off) {
        s_global_log_enabled = true;
      }
    } else {
      strlcpy(s_global_log_path, value, sizeof(s_global_log_path));
      if (!s_global_log_explicit_off) {
        s_global_log_enabled = true;
      }
    }
  }
}

bool eos_log_is_file_enabled(const char **path_out) {
  // Check current thread environment first in case it was overridden locally
  const char *t_log = eos_tctx_getenv("EOS_LOG");
  const char *t_file = eos_tctx_getenv("EOS_LOG_FILE");
  if (!t_file) {
    t_file = eos_tctx_getenv("LOG_FILE");
  }

  bool enabled = s_global_log_enabled;
  const char *path =
      s_global_log_path[0] ? s_global_log_path : EOS_DEFAULT_LOG_FILE;

  if (t_log) {
    if (is_falsy_str(t_log)) {
      enabled = false;
    } else if (is_truthy_str(t_log)) {
      enabled = true;
    } else {
      enabled = true;
      path = t_log;
    }
  }

  if (t_file && t_file[0]) {
    if (is_falsy_str(t_file)) {
      enabled = false;
    } else if (!is_truthy_str(t_file)) {
      path = t_file;
      if (!t_log || !is_falsy_str(t_log)) {
        enabled = true;
      }
    } else if (!t_log || !is_falsy_str(t_log)) {
      enabled = true;
    }
  }

  if (path_out) {
    *path_out = path;
  }
  return enabled;
}

static void write_log_to_file(const char *path, const char *buf, size_t len) {
  if (!path || !path[0] || !buf || len == 0)
    return;

  char resolved[PATH_MAX];
  if (path[0] == '/') {
    strlcpy(resolved, path, sizeof(resolved));
  } else {
    snprintf(resolved, sizeof(resolved), "/%s", path);
  }

  struct _reent *r = _REENT;
  int fd = __real__open_r(r, resolved, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0)
    return;

  struct stat st;
  if (__real__fstat_r(r, fd, &st) == 0 &&
      (size_t)st.st_size + len > EOS_LOG_MAX_BYTES) {
    __real__close_r(r, fd);
    fd = __real__open_r(r, resolved, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
      return;
  }

  (void)__real__write_r(r, fd, buf, len);
  __real__close_r(r, fd);
}

static int eos_log_vprintf(const char *fmt, va_list ap) {
  if (s_in_log_hook) {
    if (eos_console_is_connected() && s_prev_vprintf) {
      return s_prev_vprintf(fmt, ap);
    }
    return 0;
  }

  s_in_log_hook = true;

  char buf[512];
  va_list ap_copy;
  va_copy(ap_copy, ap);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap_copy);
  va_end(ap_copy);

  if (len > 0) {
    size_t write_len =
        ((size_t)len < sizeof(buf)) ? (size_t)len : (sizeof(buf) - 1U);
    const char *log_path = NULL;
    if (eos_log_is_file_enabled(&log_path)) {
      write_log_to_file(log_path, buf, write_len);
    }
  }

  int ret = len;
  if (eos_console_is_connected()) {
    if (s_prev_vprintf) {
      ret = s_prev_vprintf(fmt, ap);
    } else {
      ret = vprintf(fmt, ap);
    }
  }

  s_in_log_hook = false;
  return ret;
}

void eos_log_init(void) {
  if (s_log_initialized)
    return;
  s_log_initialized = true;
  s_prev_vprintf = esp_log_set_vprintf(eos_log_vprintf);
}
