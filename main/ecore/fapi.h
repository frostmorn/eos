#pragma once
#include <stddef.h>

#define EOS_FAPI_COUNT_BUFFERS 2

// Check main/ewrap/fapi.c for system wrappers arround esp-idf implementations
char *eos_fapi_path_resolve(char *path, char *buffer); 
void eos_fapi_path_normalize(char *path);
char *eos_fapi_get_buffer(size_t index);
