#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
//
// (^__^)==\~ tmpfs - a writable, in-memory filesystem.
//
// Design taken from keira's SPIRamVFS:
// https://github.com/lilka-dev/keira/blob/main/src/keira/vfs/spiram/spiram.cpp
// - same node/block/fd shape, same hashed-name lookup - but the
// tree itself (parent/child/next, attach/detach/rename) is built on
// EOS's own generic tree module (emisc/fancytree.h) instead of
// hand-rolled pointer surgery, since this is exactly the shape
// ecore/tree already solves once, tested, for everyone else.
//
// Nodes are allocated with plain malloc()/kvec's own realloc(), not
// heap_caps_malloc(SPIRAM). On a board with CONFIG_SPIRAM_USE_MALLOC
// enabled that transparently lands in PSRAM anyway for anything past
// the usual small-allocation threshold; if a hard SPIRAM-only
// guarantee is ever required regardless of that config, the block
// storage would need to move off kvec's built-in allocator onto a
// custom one.

#include "emisc/strlimits.h"

// Configuration:
#ifndef EOS_TMPFS_ROOT
#define EOS_TMPFS_ROOT "" // Mount as root dir
#endif

#define TMP_NODE_NAME_MAX 16
#define TMP_DIR_MODE      (S_IFDIR|0755)
#define TMP_FILE_MODE     (S_IFREG|0644)
#define TMP_SECTOR_SIZE   512

void eos_tmpfs_init();
