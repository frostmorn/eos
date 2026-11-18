#pragma once
#include <esp_log.h>

#define EOS_MASCOT_L "(^_^)==\\~"
#define EOS_MASCOT_R "~/==(^_^)"

#define EOS_MASCOT_W_L "[O_o]==\\~"
#define EOS_MASCOT_W_R "~/==[o_0]"

#define EOS_MASCOT_E_L "[-_-]==|~"
#define EOS_MASCOT_E_R "~|==[-_-]"

#define EOS_STR_HELPER(x) #x
#define EOS_STR(x) EOS_STR_HELPER(x)

#define EOS_LOGE(format, ...)                                                  \
  ESP_LOGE(EOS_MASCOT_E_R " " __FILE__ ":" EOS_STR(__LINE__), format, ##__VA_ARGS__)
#define EOS_LOGW(format, ...)                                                  \
  ESP_LOGW(EOS_MASCOT_W_R " " __FILE__ ":" EOS_STR(__LINE__), format, ##__VA_ARGS__)
#define EOS_LOGI(format, ...)                                                  \
  ESP_LOGI(EOS_MASCOT_R " " __FILE__ ":" EOS_STR(__LINE__), format, ##__VA_ARGS__)

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
#define EOS_TIMEOUT_5000_MS         5000
#define EOS_TIMEOUT_500_MS          500
#define EOS_TIMEOUT_200_MS          200
#define EOS_MS_PER_TICK_60          60
#define EOS_MS_PER_TICK_50          50
#define EOS_HEX_BYTE_MSB            0x80
#define EOS_HEX_COLOR_WHITE         0xFFFFFF
#define EOS_DEFAULT_MAGIC_42        42
#define EOS_DEFAULT_MAGIC_33        33
#define EOS_RGB565_RED_MAX          31
#define EOS_RGB565_GREEN_MAX        63

// Standard POSIX file and directory permission modes
#define EOS_DEFAULT_DIR_MODE   0755
#define EOS_DEFAULT_FILE_MODE  0644
#define EOS_DEFAULT_DEV_MODE   0666
