#include "includes.h"
#ifdef EOS_DRIVER_BUS_GPIO_ENABLED

#include "driver/bus/bus.h"
#include "driver/driver.h"

int driver_bus_gpio_ioctl(eos_dev_t *dev_bus, int cmd, ...) {
  switch (cmd) {
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

EOS_DRIVER_ATTR eos_driver_t driver_bus_gpio = {EOS_DRIVER_INIT, .scope = "bus",
                                                .name = "gpio",
                                                .ioctl = driver_bus_gpio_ioctl};

#endif