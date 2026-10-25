#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "ecore/bin.h"

int test_sdl_input_main(int argc, char **argv) {
  int max_events = 20;
  if (argc > 1) {
    max_events = atoi(argv[1]);
  }

  printf("Initializing SDL for input testing...\n");
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }

  SDL_Surface *screen = SDL_SetVideoMode(240, 280, 16, 0);
  if (!screen) {
    printf("SDL_SetVideoMode failed: %s\n", SDL_GetError());
    SDL_Quit();
    return -1;
  }

  printf("SDL input test started. Press keys (ESC to exit, max %d events)...\n", max_events);

  int count = 0;
  SDL_Event e;

  while (count < max_events) {
    if (SDL_PollEvent(&e)) {
      count++;
      if (e.type == SDL_KEYDOWN) {
        printf("[%d] SDL_KEYDOWN: sym=%d\n", count, (int)e.key.keysym.sym);
        if (e.key.keysym.sym == SDLK_ESCAPE) {
          printf("ESC pressed, exiting SDL input test.\n");
          break;
        }
      } else if (e.type == SDL_QUIT) {
        printf("SDL_QUIT received.\n");
        break;
      } else {
        printf("[%d] SDL Event type=%lu\n", count, (unsigned long)e.type);
      }
    } else {
      SDL_Delay(50);
    }
  }

  SDL_Quit();
  printf("test_sdl_input finished (%d events processed).\n", count);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sdl_input = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = 12288U,
    .filename = "test_sdl_input",
    .name = "SDL Input Test",
    .group = "Tests",
    .description = "Test SDL event queue polling and keypad keycode translation",
    .entry_point = test_sdl_input_main};
