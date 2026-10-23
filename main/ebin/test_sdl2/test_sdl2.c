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

  SDL_Window *window = SDL_CreateWindow("EOS SDL2 Test", 0, 0, 240, 280, SDL_WINDOW_SHOWN);
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

  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, 240, 280);
  if (!texture) {
    printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
  }

  uint16_t *tex_pixels = malloc(240 * 280 * 2);
  if (tex_pixels) {
    for (int i = 0; i < 240 * 280; i++) {
      tex_pixels[i] = 0xF800; // red
    }
    SDL_UpdateTexture(texture, NULL, tex_pixels, 240 * 2);
    free(tex_pixels);
  }

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);

  printf("SDL2 test rendered red frame successfully.\n");

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_sdl2 = {
    EOS_BIN_INITIALIZER, .filename = "test_sdl2", .name = "test_sdl2",
    .entry_point = test_sdl2_main};
