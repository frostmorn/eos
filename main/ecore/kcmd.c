#include "includes.h"
#include "ecore/kcmd.h"
#include "ecore/log.h"
#include "ecore/proc.h"

#include "esp_attr.h"
#include "esp_crc.h"

#include <errno.h>
#include <string.h>

typedef struct {
  char cmd[EOS_KCMD_MAX_LEN];
  uint32_t crc;
} eos_kcmd_rtc_t;

RTC_NOINIT_ATTR static eos_kcmd_rtc_t s_kcmd;

int eos_kcmd_set(const char *cmd) {
  if (!cmd) {
    memset(s_kcmd.cmd, 0, sizeof(s_kcmd.cmd));
    s_kcmd.crc = 0;
    return 0;
  }
  size_t len = strlen(cmd);
  if (len >= sizeof(s_kcmd.cmd)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memset(s_kcmd.cmd, 0, sizeof(s_kcmd.cmd));
  memcpy(s_kcmd.cmd, cmd, len);
  s_kcmd.crc = esp_crc32_le(0, (const uint8_t *)s_kcmd.cmd, sizeof(s_kcmd.cmd));
  return 0;
}

bool eos_kcmd_consume(char *out_buf, size_t out_sz) {
  if (!out_buf || out_sz == 0) {
    return false;
  }
  uint32_t expected = esp_crc32_le(0, (const uint8_t *)s_kcmd.cmd, sizeof(s_kcmd.cmd));
  if (s_kcmd.crc != expected) {
    out_buf[0] = '\0';
    return false;
  }
  s_kcmd.cmd[sizeof(s_kcmd.cmd) - 1] = '\0';
  strlcpy(out_buf, s_kcmd.cmd, out_sz);
  // Invalidate CRC immediately so kcmd is strictly one-shot
  s_kcmd.crc ^= UINT32_MAX;
  memset(s_kcmd.cmd, 0, sizeof(s_kcmd.cmd));
  EOS_LOGI("kcmd: consumed boot args '%s'", out_buf);
  return out_buf[0] != '\0';
}

void eos_reboot_with_kcmd(const char *cmd) {
  if (cmd && cmd[0] != '\0') {
    (void)eos_kcmd_set(cmd);
  }
  eos_reboot(false);
}
