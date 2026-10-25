#include <stdio.h>
#include "ecore/bin.h"
#include "SDL2/SDL.h"

static int test_sdl2_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("Starting SDL2 compatibility test...\n");

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL2 SDL_Init failed: %s\n", SDL_GetError());
    return -1;
  }

  // Pass 0, 0 to automatically detect screen dimensions via ioctl inside SDL2 implementation
  SDL_Window *window = SDL_CreateWindow("EOS SDL2 Test", 0, 0, 0, 0, SDL_WINDOW_SHOWN);
  if (!window) {
    printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return -1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
  }

  // Pass 0, 0 to SDL_CreateTexture to automatically use window/screen dimensions
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, 0, 0);
  if (!texture) {
    printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
  }

  int w, h;
  SDL_QueryTexture(texture, NULL, NULL, &w, &h);
  printf("SDL2 display resolution determined via ioctl: %dx%d\n", w, h);

  uint16_t *tex_pixels = malloc(w * h * 2);
  if (tex_pixels) {
    for (int i = 0; i < w * h; i++) {
      tex_pixels[i] = 0xF800; // red
    }
    SDL_UpdateTexture(texture, NULL, tex_pixels, w * 2);
    free(tex_pixels);
  }

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);

  printf("SDL2 test rendered red frame successfully (%dx%d).\n", w, h);

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sdl2 = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = 12288U,
    .filename = "test_sdl2",
    .name = "SDL2 Test",
    .group = "Tests",
    .description = "Test SDL2 window, software renderer, and texture rendering pipelines",
    .entry_point = test_sdl2_main};
