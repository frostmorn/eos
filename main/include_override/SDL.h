#pragma once
#include "SDL_stdinc.h"
#include "SDL_error.h"
#include "SDL_rect.h"
#include "SDL_surface.h"
#include "SDL_video.h"
#include "SDL_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_INIT_VIDEO 0x00000020
#define SDL_INIT_AUDIO 0x00000010
#define SDL_INIT_TIMER 0x00000001

int SDL_Init(Uint32 flags);
void SDL_Quit(void);

#ifdef __cplusplus
}
#endif
