#include "error.h"

eos_error_t eos_errno = EOS_ERR_NO_ERROR;

static const char *eos_error_strings[] = {
    [EOS_ERR_NO_ERROR] = "no error",
    [EOS_ERR_NO_MEM_LEFT_ERROR] = "no memory left",
    [EOS_ERR_DEVICE_DRIVER_INVALID] = "invalid driver provided",
    [EOS_ERR_DRIVER_ALREADY_REGISTERED] = "driver already registered",
    [EOS_ERR_DEVICE_COUNT_QUOTA_EXCEED] = "device count quota exceeded",
    [EOS_ERR_DEVICE_BUS_INVALID] = "bus device invalid",
    [EOS_ERR_DEVICE_INVALID] = "device invalid",
    [EOS_ERR_DEVICE_ALREADY_ATTACHED] = "device already attached",
    [EOS_ERR_DEVICE_ATTACH_DECLINED_BY_BUS] = "attach declined by bus",
    [EOS_ERR_DEVICE_DETACH_DECLINED_BY_BUS] = "detach declined by bus",
    [EOS_ERR_DEVICE_INIT_FAILED] = "device driver init failed",
    [EOS_ERR_CAP_INVALID] = "invalid capability id provided",
    [EOS_ERR_CAP_COUNT_QUOTA_EXCEED] = "not capability block slots left",
    [EOS_ERR_CAP_NO_INVALID] = "wrong capability number provided",
    [EOS_ERR_CAP_NO_RESERVED] = "current capability number reserved"};

char *eos_error_to_str(eos_error_t err) {
  if (err >= sizeof(eos_error_strings) / sizeof(*eos_error_strings))
    return "unknown error";
  return (char *)eos_error_strings[err];
}
