#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// This component of EOS is responsible for a filesystem
// representation of EOS device tree

#include "sys/device.h"

#ifndef EOS_DEVFS_ROOT
#define EOS_DEVFS_ROOT "/dev"
#endif

// Inits EOS devfs
void eos_devfs_init();
