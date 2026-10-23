#pragma once

#ifdef __cplusplus
extern "C" {
#endif

const char *SDL_GetError(void);
void SDL_SetError(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
