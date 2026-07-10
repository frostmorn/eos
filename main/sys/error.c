#include "error.h"

eos_error_t eos_errno = EOS_NO_ERROR;

static const char *eos_error_strings[] = {
    [EOS_NO_ERROR] = "no error",
    [EOS_NO_MEM_LEFT_ERROR] = "no memory left",
    [EOS_DEVICE_COUNT_QUOTA_EXCEED] = "device count quota exceeded",
    [EOS_DEVICE_BUS_INVALID] = "bus device invalid",
    [EOS_DEVICE_INVALID] = "device invalid",
    [EOS_DEVICE_ALREADY_ATTACHED] = "device already attached",
    [EOS_DEVICE_ATTACH_DECLINED_BY_BUS] = "attach declined by bus",
    [EOS_DEVICE_DETACH_DECLINED_BY_BUS] = "detach declined by bus",
};

char *eos_error_to_str(eos_error_t err) {
  if (err >= sizeof(eos_error_strings) / sizeof(*eos_error_strings))
    return "unknown error";
  return (char *)eos_error_strings[err];
}