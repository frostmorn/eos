#pragma once

#include "ecore/fs.h"

int eos_fat_mount(const char *source, const char *target, uint32_t flags, const char *opts, void **out_ctx);
int eos_fat_umount(const char *target, void *ctx, uint32_t flags);
