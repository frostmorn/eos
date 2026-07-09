#include "drivers/bus/bus.h"
#include "includes.h"
#include "sys/driver.h"
// This driver is always enabled,
// and represents root bus which is an origin point of our device tree

int driver_bus_root_init(eos_dev_t *dev_bus) { return 0; }

int driver_bus_root_read(eos_dev_t *dev_bus, void *buf, size_t len) {
  return 0;
}

int driver_bus_root_write(eos_dev_t *dev_bus, void *buf, size_t len) {
  return 0;
}

int driver_bus_root_ioctl(eos_dev_t *dev_bus, int cmd, va_list args) {
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

void driver_bus_root_shutdown(eos_dev_t *dev_bus) {}

EOS_DRIVER_REG(bus, root, EOS_INIT_DRIVERS_ROOT_BUS);
