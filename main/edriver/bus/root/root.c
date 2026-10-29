#include "includes.h"

#ifndef EOS_DRV_BUS_ROOT_ENABLED
#define EOS_DRV_BUS_ROOT_ENABLED
#endif

#ifdef EOS_DRV_BUS_ROOT_ENABLED
#include "ecore/driver.h"
#include "ecore/ioctl.h"

// Root bus driver representing the origin point of our device tree
EOS_DRV_ATTR eos_drv_t driver_bus_root = {
  EOS_DRV_INIT, 
  .flags = EOS_DRV_FLAG_NO_VFS | EOS_DRV_FLAG_NO_INDEX,
  .scope = "bus",
  .name = "root",
  .devname = "root"
};
#endif
