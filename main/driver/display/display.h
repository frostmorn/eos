#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// Reserved ioctl commands every bus driver must recognize, in addition
// to its own scope-specific commands. Display-specific commands (GET_WIDTH,
// GET_HEIGHT) must start at EOS_DISPLAY_IOCTL_USER_BASE or above —
// values below it are framework-reserved and apply to every bus.
typedef enum {
  EOS_DISPLAY_IOCTL_GET_WIDTH = 0, // returns display width
  EOS_DISPLAY_IOCTL_GET_HEIGHT,    // returns display height
  EOS_DISPLAY_IOCTL_USER_BASE = 100
} eos_bus_ioctl_t;