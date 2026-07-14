#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

typedef enum {
  EOS_ERR_NO_ERROR,
  EOS_ERR_NO_MEM_LEFT_ERROR,
  EOS_ERR_DEVICE_DRIVER_INVALID,
  EOS_ERR_DRIVER_ALREADY_REGISTERED,
  EOS_ERR_DEVICE_COUNT_QUOTA_EXCEED,
  EOS_ERR_DEVICE_BUS_INVALID,
  EOS_ERR_DEVICE_INVALID,
  EOS_ERR_DEVICE_ALREADY_ATTACHED,
  EOS_ERR_DEVICE_ATTACH_DECLINED_BY_BUS,
  EOS_ERR_DEVICE_DETACH_DECLINED_BY_BUS,
  EOS_ERR_DEVICE_INIT_FAILED,
  EOS_ERR_CAP_INVALID,
  EOS_ERR_CAP_COUNT_QUOTA_EXCEED,
  EOS_ERR_CAP_NO_INVALID,
  EOS_ERR_CAP_NO_RESERVED
} eos_error_t;

extern eos_error_t eos_errno;

// Returns string representation of specified error
char *eos_error_to_str(eos_error_t err);