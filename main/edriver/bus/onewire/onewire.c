#include "includes.h"
#ifdef EOS_DRV_BUS_ONEWIRE_ENABLED

#include "ecore/driver.h"

EOS_DRV_ATTR eos_drv_t driver_bus_onewire = {
    EOS_DRV_INIT,
    .scope = "bus",
    .name = "onewire",
};

#endif
