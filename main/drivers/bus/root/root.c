#include "driver.h"
#include "includes.h"

// This driver is always enabled,
// and represents root bus which is an origin point of our device tree

int driver_bus_root_init(eos_dev_t *dev) { return 0;}

int driver_bus_root_read(eos_dev_t *dev, void *buf, size_t len) {return 0;}

int driver_bus_root_write(eos_dev_t *dev, void *buf, size_t len) {return 0;}

int driver_bus_root_ioctl(eos_dev_t *dev, int cmd, va_list args) {return 0;}

void driver_bus_root_shutdown(eos_dev_t *dev) {}

EOS_DRIVER_REG(bus, root, EOS_INIT_DRIVERS_ROOT_BUS);
