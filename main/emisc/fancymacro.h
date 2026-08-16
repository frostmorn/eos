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
