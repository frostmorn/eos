#pragma once
#include "SDL_stdinc.h"
#include "SDL_rect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SDL_NOEVENT = 0,
  SDL_QUIT = 0x100,
  SDL_KEYDOWN = 0x300,
  SDL_KEYUP = 0x301
} SDL_EventType;

#define SDLK_UNKNOWN      0
#define SDLK_RETURN       13
#define SDLK_ESCAPE       27
#define SDLK_SPACE        32
#define SDLK_UP           273
#define SDLK_DOWN         274
#define SDLK_RIGHT        275
#define SDLK_LEFT         276
#define SDLK_a            97
#define SDLK_b            98
#define SDLK_c            99
#define SDLK_d            100

typedef struct {
  Uint8 scancode;
  Uint16 sym;
  Uint16 mod;
  Uint16 unicode;
} SDL_Keysym;

typedef struct {
  Uint32 type;
  Uint8 state;
  SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct {
  Uint32 type;
} SDL_QuitEvent;

typedef union {
  Uint32 type;
  SDL_KeyboardEvent key;
  SDL_QuitEvent quit;
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
void SDL_PumpEvents(void);

#ifdef __cplusplus
}
#endif
