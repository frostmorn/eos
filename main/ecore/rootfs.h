#pragma once
#include <esp_vfs.h>
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// Doesn't implicate esp_vfs layer
void eos_vfs_register_dummy(const char *base_path);
void eos_vfs_unregister_dummy(const char *base_path);

// Wrappers arround esp_vfs*
esp_err_t eos_vfs_register(const char* base_path, const esp_vfs_t* vfs, void* ctx);
esp_err_t eos_vfs_unregister(const char* base_path);

void eos_rootfs_init(void);
