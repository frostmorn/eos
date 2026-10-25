#pragma once
#include "SDL_stdinc.h"
#include "SDL_rect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SDL_QUIT = 0x100,
  SDL_KEYDOWN = 0x300,
  SDL_KEYUP = 0x301,
  SDL_MOUSEMOTION = 0x400,
  SDL_MOUSEBUTTONDOWN = 0x401,
  SDL_MOUSEBUTTONUP = 0x402,
  SDL_MOUSEWHEEL = 0x403,
  SDL_FINGERDOWN = 0x700,
  SDL_FINGERUP = 0x701,
  SDL_FINGERMOTION = 0x702
} SDL_EventType;

#define SDL_RELEASED 0
#define SDL_PRESSED  1

#define SDL_BUTTON_LEFT      1
#define SDL_BUTTON_MIDDLE    2
#define SDL_BUTTON_RIGHT     3
#define SDL_BUTTON(X)        (1 << ((X)-1))
#define SDL_BUTTON_LMASK     SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK     SDL_BUTTON(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK     SDL_BUTTON(SDL_BUTTON_RIGHT)

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

typedef struct {
  Uint32 type;
  Uint32 timestamp;
  Uint32 windowID;
  Uint32 which;
  Uint8 button;
  Uint8 state;
  Uint8 clicks;
  Uint8 padding1;
  Sint32 x;
  Sint32 y;
} SDL_MouseButtonEvent;

typedef struct {
  Uint32 type;
  Uint32 timestamp;
  Uint32 windowID;
  Uint32 which;
  Uint32 state;
  Sint32 x;
  Sint32 y;
  Sint32 xrel;
  Sint32 yrel;
} SDL_MouseMotionEvent;

typedef int64_t SDL_TouchID;
typedef int64_t SDL_FingerID;

typedef struct {
  Uint32 type;
  Uint32 timestamp;
  SDL_TouchID touchId;
  SDL_FingerID fingerId;
  float x;
  float y;
  float dx;
  float dy;
  float pressure;
  Uint32 windowID;
} SDL_TouchFingerEvent;

typedef union {
  Uint32 type;
  SDL_KeyboardEvent key;
  SDL_MouseButtonEvent button;
  SDL_MouseMotionEvent motion;
  SDL_TouchFingerEvent tfinger;
  SDL_QuitEvent quit;
  Uint8 padding[56];
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
void SDL_PumpEvents(void);
Uint32 SDL_GetMouseState(int *x, int *y);

#ifdef __cplusplus
}
#endif
