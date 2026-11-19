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

// Initializes and mounts virtual /proc filesystem
void eos_procfs_init(void);

#ifdef __cplusplus
}
#endif
