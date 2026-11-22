#pragma once

#if defined(__has_include)
#  if __has_include(<esp_log.h>)
#    include <esp_log.h>
#    define EOS_HAS_ESP_LOG 1
#  endif
#endif

#define EOS_MASCOT_L "(^_^)==\\~"
#define EOS_MASCOT_R "~/==(^_^)"

#define EOS_MASCOT_W_L "[O_o]==\\~"
#define EOS_MASCOT_W_R "~/==[o_0]"

#define EOS_MASCOT_E_L "[-_-]==|~"
#define EOS_MASCOT_E_R "~|==[-_-]"

#define EOS_STR_HELPER(x) #x
#define EOS_STR(x) EOS_STR_HELPER(x)

#if defined(EOS_HAS_ESP_LOG)
#define EOS_LOGE(format, ...)                                                  \
  ESP_LOGE(EOS_MASCOT_E_R " " __FILE__ ":" EOS_STR(__LINE__), format, ##__VA_ARGS__)
#define EOS_LOGW(format, ...)                                                  \
  ESP_LOGW(EOS_MASCOT_W_R " " __FILE__ ":" EOS_STR(__LINE__), format, ##__VA_ARGS__)
#define EOS_LOGI(format, ...)                                                  \
  ESP_LOGI(EOS_MASCOT_R " " __FILE__ ":" EOS_STR(__LINE__), format, ##__VA_ARGS__)
#else
#include <stdio.h>
#define EOS_LOGE(format, ...) fprintf(stderr, "E " EOS_MASCOT_E_R " " __FILE__ ":" EOS_STR(__LINE__) " " format "\n", ##__VA_ARGS__)
#define EOS_LOGW(format, ...) fprintf(stderr, "W " EOS_MASCOT_W_R " " __FILE__ ":" EOS_STR(__LINE__) " " format "\n", ##__VA_ARGS__)
#define EOS_LOGI(format, ...) printf("I " EOS_MASCOT_R " " __FILE__ ":" EOS_STR(__LINE__) " " format "\n", ##__VA_ARGS__)
#endif

#define EOS_ARR_COUNT(ARR) (sizeof(ARR) / sizeof(ARR[0]))

// Standard time conversion constants
#define EOS_MS_PER_SEC 1000
#define EOS_US_PER_MS  1000
#define EOS_US_PER_SEC 1000000
#define EOS_SECS_PER_MIN  60
#define EOS_MINS_PER_HOUR 60
#define EOS_SECS_PER_HOUR 3600
#define EOS_SECS_PER_DAY  86400
#define EOS_DEFAULT_POLL_TIMEOUT_US 10000
#define EOS_DEFAULT_TIMEOUT_MS      10000
#define EOS_EXTENDED_TIMEOUT_MS         5000
#define EOS_HALF_SEC_TIMEOUT_MS          500
#define EOS_PROBE_TIMEOUT_MS          200
#define EOS_DISPLAY_REFRESH_INTERVAL_MS          60
#define EOS_POLL_INTERVAL_MS          50
#define EOS_HEX_BYTE_MSB            0x80
#define EOS_HEX_COLOR_WHITE         0xFFFFFF
#define EOS_TEST_MAGIC_STATUS        42
#define EOS_TEST_MAGIC_ALT        33
#define EOS_RGB565_RED_MAX          31
#define EOS_RGB565_GREEN_MAX        63

// Standard POSIX file and directory permission modes
#define EOS_DEFAULT_DIR_MODE   0755
#define EOS_DEFAULT_FILE_MODE  0644
#define EOS_DEFAULT_DEV_MODE   0666
