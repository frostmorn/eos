#pragma once
#include <stddef.h>

// Check main/ewrap/fapi.c for system wrappers arround esp-idf implementations
char *eos_fapi_path_resolve(char *path, char *buffer); 
char *eos_fapi_get_buffer(size_t index);
