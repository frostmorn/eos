#include "includes.h"
#ifdef EOS_DRIVER_BUS_GPIO_ENABLED

#include "drivers/bus/bus.h"
#include "sys/driver.h"

int driver_bus_gpio_init(eos_dev_t *dev_bus) { return 0; }

int driver_bus_gpio_read(eos_dev_t *dev_bus, void *buf, size_t len) {
  return 0;
}

int driver_bus_gpio_write(eos_dev_t *dev_bus, void *buf, size_t len) {
  return 0;
}

int driver_bus_gpio_ioctl(eos_dev_t *dev_bus, int cmd, ...) {
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

void driver_bus_gpio_shutdown(eos_dev_t *dev_bus) {}

EOS_DRIVER_REG(bus, gpio, EOS_INIT_DRIVERS_BUS);

#endif