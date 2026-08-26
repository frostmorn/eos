#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// This component of EOS is responsible for a filesystem
// representation of native apps in a form of
// native manifest files

#include "ecore/bin.h"

#ifndef EOS_BINFS_ROOT
#define EOS_BINFS_ROOT "/bin"
#endif

#ifndef EOS_BINFS_MAX_FDS
#define EOS_BINFS_MAX_FDS 16
#endif

// Inits EOS binfs
void eos_binfs_init(void);
