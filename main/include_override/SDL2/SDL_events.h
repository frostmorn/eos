#pragma once
#include "SDL_stdinc.h"
#include "SDL_rect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SDL_QUIT = 0x100,
  SDL_KEYDOWN = 0x300,
  SDL_KEYUP = 0x301
} SDL_EventType;

typedef int SDL_Scancode;
typedef int SDL_Keycode;

#define SDLK_UNKNOWN      0
#define SDLK_RETURN       13
#define SDLK_ESCAPE       27
#define SDLK_SPACE        32
#define SDLK_UP           1073741906
#define SDLK_DOWN         1073741905
#define SDLK_RIGHT        1073741903
#define SDLK_LEFT         1073741904
#define SDLK_a            97
#define SDLK_b            98
#define SDLK_c            99
#define SDLK_d            100

typedef struct {
  SDL_Scancode scancode;
  SDL_Keycode sym;
  Uint16 mod;
  Uint32 unused;
} SDL_Keysym;

typedef struct {
  Uint32 type;
  Uint32 timestamp;
  Uint32 windowID;
  Uint8 state;
  Uint8 repeat;
  Uint8 padding2;
  Uint8 padding3;
  SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct {
  Uint32 type;
  Uint32 timestamp;
} SDL_QuitEvent;

typedef union {
  Uint32 type;
  SDL_KeyboardEvent key;
  SDL_QuitEvent quit;
  Uint8 padding[56];
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
void SDL_PumpEvents(void);

#ifdef __cplusplus
}
#endif
