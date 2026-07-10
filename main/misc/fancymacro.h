#pragma once
#include <esp_log.h>
#define EOS_STR_HELPER(x) #x
#define EOS_STR(x) EOS_STR_HELPER(x)

#define EOS_LOGE(format, ...) ESP_LOGE(__FILE__, format, ##__VA_ARGS__)
