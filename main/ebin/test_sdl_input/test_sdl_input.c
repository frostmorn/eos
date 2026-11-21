#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "emisc/fancymacro.h"

int test_sdl_input_main(int argc, char **argv) {
  int max_events = 20;
  int timeout_ms = -1;
  bool auto_mode = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto") == 0) {
      auto_mode = true;
      if (timeout_ms < 0) {
        timeout_ms = EOS_MS_PER_SEC;
      }
      max_events = 1;
    } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) && i + 1 < argc) {
      timeout_ms = atoi(argv[++i]);
    } else if (argv[i][0] != '-') {
      max_events = atoi(argv[i]);
    }
  }
  (void)auto_mode;

  printf("Initializing SDL for input testing...\n");
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }

  SDL_Surface *screen = SDL_SetVideoMode(EOS_DISPLAY_DEFAULT_HEIGHT, EOS_DISPLAY_DEFAULT_WIDTH, 16, 0);
  if (!screen) {
    printf("SDL_SetVideoMode failed: %s\n", SDL_GetError());
    SDL_Quit();
    return -1;
  }

  printf("SDL input test started. Press keys (ESC to exit, max %d events, timeout %d ms)...\n",
         max_events, timeout_ms);

  int count = 0;
  SDL_Event e;
  uint32_t start_ms = SDL_GetTicks();

  while (count < max_events) {
    if (timeout_ms >= 0) {
      uint32_t elapsed = SDL_GetTicks() - start_ms;
      if (elapsed >= (uint32_t)timeout_ms) {
        printf("Timeout (%d ms) reached.\n", timeout_ms);
        break;
      }
    }

    if (SDL_PollEvent(&e)) {
      count++;
      if (e.type == SDL_KEYDOWN) {
        printf("[%d] SDL_KEYDOWN: sym=%d\n", count, (int)e.key.keysym.sym);
        if (e.key.keysym.sym == SDLK_ESCAPE) {
          printf("ESC pressed, exiting SDL input test.\n");
          break;
        }
      } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        printf("[%d] SDL_MOUSEBUTTONDOWN: btn=%d at (%d, %d)\n", count,
               (int)e.button.button, (int)e.button.x, (int)e.button.y);
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        printf("[%d] SDL_MOUSEBUTTONUP: btn=%d at (%d, %d)\n", count,
               (int)e.button.button, (int)e.button.x, (int)e.button.y);
      } else if (e.type == SDL_MOUSEMOTION) {
        printf("[%d] SDL_MOUSEMOTION: pos=(%d, %d), rel=(%d, %d)\n", count,
               (int)e.motion.x, (int)e.motion.y, (int)e.motion.xrel, (int)e.motion.yrel);
      } else if (e.type == SDL_QUIT) {
        printf("SDL_QUIT received.\n");
        break;
      } else {
        printf("[%d] SDL Event type=%lu\n", count, (unsigned long)e.type);
      }
    } else {
      SDL_Delay(EOS_MS_PER_TICK_50);
    }
  }

  SDL_Quit();
  printf("test_sdl_input finished (%d events processed).\n", count);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sdl_input = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_LARGE_STACK_SIZE,
    .filename = "test_sdl_input",
    .name = "SDL Input Test",
    .group = "Tests",
    .description = "Test SDL event queue polling and keypad keycode translation",
    .entry_point = test_sdl_input_main};
