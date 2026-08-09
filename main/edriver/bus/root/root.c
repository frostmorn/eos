#include "ecore/ioctl.h"
#include "ecore/driver.h"
#include "includes.h"
// This driver is always enabled,
// and represents root bus which is an origin point of our device tree

// Do I actually need that crap?

EOS_DRIVER_ATTR eos_driver_t driver_bus_root = {EOS_DRIVER_INIT, .scope = "bus",
                                                .name = "root"};
