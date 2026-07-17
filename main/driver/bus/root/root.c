#include "driver/bus/bus.h"
#include "driver/driver.h"
#include "includes.h"
// This driver is always enabled,
// and represents root bus which is an origin point of our device tree

int driver_bus_root_ioctl(eos_dev_t *dev_bus, int cmd, ...) {
  switch (cmd) {
    // ROOT BUS always accepts all ATTACH/DETACH requests
  case EOS_BUS_IOCTL_KID_ATTACH: {
    return true;
    break;
  }
  case EOS_BUS_IOCTL_KID_DETACH: {
    return true;
    break;
  }
  }

  return 0;
}

EOS_DRIVER_ATTR eos_driver_t driver_bus_root = {EOS_DRIVER_INIT, .scope = "bus",
                                                .name = "root",
                                                .ioctl = driver_bus_root_ioctl};
