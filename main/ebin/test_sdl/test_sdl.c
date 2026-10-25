#include <stdio.h>
#include "ecore/bin.h"
#include "SDL.h"

static int test_sdl_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("Starting SDL 1.2 compatibility test...\n");

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }

  // Pass 0, 0 to automatically detect screen dimensions via ioctl inside SDL implementation
  SDL_Surface *screen = SDL_SetVideoMode(0, 0, 16, SDL_SWSURFACE);
  if (!screen) {
    printf("SDL_SetVideoMode failed: %s\n", SDL_GetError());
    SDL_Quit();
    return -1;
  }

  printf("SDL video mode set with max dimensions: %dx%d\n", screen->w, screen->h);

  uint16_t *pixels = screen->pixels;
  for (int i = 0; i < screen->w * screen->h; i++) {
    pixels[i] = 0x07E0; // green
  }

  SDL_Flip(screen);
  printf("SDL 1.2 test rendered green frame successfully.\n");

  SDL_Quit();
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sdl = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = 12288U,
    .filename = "test_sdl",
    .name = "test_sdl",
    .group = "Tests",
    .entry_point = test_sdl_main};
