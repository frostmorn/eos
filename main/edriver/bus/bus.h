#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// Reserved ioctl commands every bus driver must recognize, in addition
// to its own scope-specific commands. Bus-specific commands (SPI mode,
// GPIO direction, ...) must start at EOS_BUS_IOCTL_USER_BASE or above —
// values below it are framework-reserved and apply to every bus.
typedef enum {
  EOS_BUS_IOCTL_KID_ATTACH = 0,  // args: eos_dev_t *child
  EOS_BUS_IOCTL_KID_DETACH,      // args: eos_dev_t *child

  EOS_BUS_IOCTL_LOCK,            // acquire exclusive bus access before I/O
  EOS_BUS_IOCTL_UNLOCK,          // release it after

  EOS_BUS_IOCTL_USER_BASE = 100
} eos_bus_ioctl_t;