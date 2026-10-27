#include "includes.h"
#ifdef EOS_DRV_SOUND_MAX98357_ENABLED

#include "ecore/driver.h"

EOS_DRV_ATTR eos_drv_t driver_sound_max98357 = {
    EOS_DRV_INIT,
    .scope = "sound",
    .name = "max98357",
};

#endif
