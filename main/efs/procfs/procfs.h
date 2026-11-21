#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
// Virtual /proc filesystem
///////////////////////////////////////////////////////

#include "includes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_PROCFS_ROOT "/proc"

#ifdef CONFIG_FREERTOS_NUMBER_OF_CORES
#define EOS_PROCFS_MAX_CORES CONFIG_FREERTOS_NUMBER_OF_CORES
#else
#define EOS_PROCFS_MAX_CORES 1
#endif

// Initializes and mounts virtual /proc filesystem
void eos_procfs_init(void);

#ifdef __cplusplus
}
#endif
