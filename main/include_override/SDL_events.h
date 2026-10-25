#pragma once
#include "SDL_stdinc.h"
#include "SDL_rect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SDL_NOEVENT = 0,
  SDL_ACTIVEEVENT = 1,
  SDL_QUIT = 0x100,
  SDL_KEYDOWN = 0x300,
  SDL_KEYUP = 0x301,
  SDL_MOUSEMOTION = 0x400,
  SDL_MOUSEBUTTONDOWN = 0x401,
  SDL_MOUSEBUTTONUP = 0x402
} SDL_EventType;

#define SDL_RELEASED 0
#define SDL_PRESSED  1

#define SDL_BUTTON_LEFT      1
#define SDL_BUTTON_MIDDLE    2
#define SDL_BUTTON_RIGHT     3
#define SDL_BUTTON_WHEELUP   4
#define SDL_BUTTON_WHEELDOWN 5
#define SDL_BUTTON(X)        (1 << ((X)-1))
#define SDL_BUTTON_LMASK     SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK     SDL_BUTTON(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK     SDL_BUTTON(SDL_BUTTON_RIGHT)

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

typedef struct {
  Uint32 type;
  Uint8 which;
  Uint8 button;
  Uint8 state;
  Uint16 x, y;
} SDL_MouseButtonEvent;

typedef struct {
  Uint32 type;
  Uint8 which;
  Uint8 state;
  Uint16 x, y;
  Sint16 xrel, yrel;
} SDL_MouseMotionEvent;

typedef union {
  Uint32 type;
  SDL_KeyboardEvent key;
  SDL_MouseButtonEvent button;
  SDL_MouseMotionEvent motion;
  SDL_QuitEvent quit;
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
void SDL_PumpEvents(void);
Uint8 SDL_GetMouseState(int *x, int *y);

#ifdef __cplusplus
}
#endif
