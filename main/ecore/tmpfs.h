#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
#include "emisc/strlimits.h"

// Configuration :

#define TMP_NODE_NAME_MAX 16
#define TMP_DIR_MODE      (S_IFDIR|0755)
#define TMP_SECTOR_SIZE   512

void eos_tmpfs_mount(const char *path);
