#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

typedef enum {
  EOS_NO_ERROR,
  EOS_NO_MEM_LEFT_ERROR,
  EOS_DEVICE_COUNT_QUOTA_EXCEED,
  EOS_DEVICE_BUS_INVALID,
  EOS_DEVICE_INVALID,
  EOS_DEVICE_ALREADY_ATTACHED,
  EOS_DEVICE_ATTACH_DECLINED_BY_BUS
} eos_error_t;

extern eos_error_t eos_errno;

// Returns string representation of specified error
char *eos_error_to_str(eos_error_t err);