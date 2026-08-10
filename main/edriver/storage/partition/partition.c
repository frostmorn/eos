#include "includes.h"
#include "ecore/diskpart.h"
#include "ecore/driver.h"

#ifdef EOS_DRIVER_STORAGE_PARTITION_ENABLED
// This is a special case driver sense of which to be a part of a disk

EOS_DRIVER_ATTR eos_driver_t driver_storage_partition = {
    EOS_DRIVER_INIT,
    .scope = "storage",
    .name = "partition"
};

#endif
