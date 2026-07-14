#include "includes.h"
#ifdef EOS_DRIVER_BUS_GPIO_ENABLED

#include "drivers/bus/bus.h"
#include "sys/driver.h"

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

EOS_DRIVER_ATTR eos_driver_t driver_bus_gpio = {
    .scope = "bus", .name = "gpio", .ioctl = driver_bus_gpio_ioctl};

EOS_DRIVER_REG(driver_bus_gpio, EOS_INIT_DRIVERS_BUS);

#endif