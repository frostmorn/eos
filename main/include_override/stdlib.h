#pragma once

#ifdef __cplusplus
extern "C" {
#endif

char *eos_proc_getenv(const char *name);
int eos_proc_setenv(const char *name, const char *value, int overwrite);
int eos_proc_unsetenv(const char *name);

#define getenv(name) eos_proc_getenv(name)
#define setenv(name, value, overwrite) eos_proc_setenv(name, value, overwrite)
#define unsetenv(name) eos_proc_unsetenv(name)

#ifdef __cplusplus
}
#endif

#include_next <stdlib.h>
