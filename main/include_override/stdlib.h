#pragma once

#ifdef __cplusplus
extern "C" {
#endif

char *eos_tctx_getenv(const char *name);
int eos_tctx_setenv(const char *name, const char *value, int overwrite);
int eos_tctx_unsetenv(const char *name);

#define getenv(name) eos_tctx_getenv(name)
#define setenv(name, value, overwrite) eos_tctx_setenv(name, value, overwrite)
#define unsetenv(name) eos_tctx_unsetenv(name)

#ifdef __cplusplus
}
#endif

#include_next <stdlib.h>
