#include "includes.h"
#ifdef EOS_DRIVER_BUS_GPIO_ENABLED

#include "ecore/ioctl.h"
#include "ecore/driver.h"


EOS_DRIVER_ATTR eos_driver_t driver_bus_gpio = {EOS_DRIVER_INIT, .scope = "bus",
                                                .name = "gpio"};

#endif
