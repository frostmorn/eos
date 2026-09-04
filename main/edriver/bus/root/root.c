#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "includes.h"
// This driver is always enabled,
// and represents root bus which is an origin point of our device tree

// Do I actually need that crap?

EOS_DRV_ATTR eos_drv_t driver_bus_root = {EOS_DRV_INIT, .scope = "bus",
                                                .name = "root"};
