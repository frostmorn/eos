#include "ecore/elf.h"
#include "emisc/fancymacro.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "SDL.h"
#include "esp_elf.h"

// Explicit alias for EOS system() implementation
int eos_system(const char *cmdline);

// Forward declarations for SDL2 functions to avoid header conflicts with SDL 1.2
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

extern SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags);
extern void SDL_DestroyWindow(SDL_Window *window);
extern SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, uint32_t flags);
extern void SDL_DestroyRenderer(SDL_Renderer *renderer);
extern SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format, int access, int w, int h);
extern void SDL_DestroyTexture(SDL_Texture *texture);
extern int SDL_UpdateTexture(SDL_Texture *texture, const void *rect, const void *pixels, int pitch);
extern int SDL_QueryTexture(SDL_Texture *texture, uint32_t *format, int *access, int *w, int *h);
extern int SDL_RenderClear(SDL_Renderer *renderer);
extern int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const void *srcrect, const void *dstrect);
extern void SDL_RenderPresent(SDL_Renderer *renderer);

static const struct esp_elfsym g_eos_posix_elfsyms[] = {
    ESP_ELFSYM_EXPORT(open),
    ESP_ELFSYM_EXPORT(read),
    ESP_ELFSYM_EXPORT(write),
    ESP_ELFSYM_EXPORT(close),
    ESP_ELFSYM_EXPORT(lseek),
    ESP_ELFSYM_EXPORT(stat),
    ESP_ELFSYM_EXPORT(fstat),
    ESP_ELFSYM_EXPORT(ioctl),
    ESP_ELFSYM_EXPORT(opendir),
    ESP_ELFSYM_EXPORT(readdir),
    ESP_ELFSYM_EXPORT(closedir),
    ESP_ELFSYM_EXPORT(mkdir),
    ESP_ELFSYM_EXPORT(rmdir),
    ESP_ELFSYM_EXPORT(unlink),
    ESP_ELFSYM_EXPORT(rename),
    ESP_ELFSYM_EXPORT(chdir),
    ESP_ELFSYM_EXPORT(getcwd),
    ESP_ELFSYM_EXPORT(system),
    ESP_ELFSYM_EXPORT(eos_system),
    ESP_ELFSYM_EXPORT(usleep),
    ESP_ELFSYM_EXPORT(sleep),
    ESP_ELFSYM_EXPORT(fopen),
    ESP_ELFSYM_EXPORT(fread),
    ESP_ELFSYM_EXPORT(fwrite),
    ESP_ELFSYM_EXPORT(fclose),
    ESP_ELFSYM_EXPORT(fseek),
    ESP_ELFSYM_EXPORT(ftell),
    ESP_ELFSYM_EXPORT(rewind),
    ESP_ELFSYM_EXPORT(fflush),
    ESP_ELFSYM_EXPORT(fgets),
    ESP_ELFSYM_EXPORT(fputs),
    ESP_ELFSYM_END
};

static const struct esp_elfsym g_eos_sdl_elfsyms[] = {
    // SDL 1.2 core & video
    ESP_ELFSYM_EXPORT(SDL_Init),
    ESP_ELFSYM_EXPORT(SDL_Quit),
    ESP_ELFSYM_EXPORT(SDL_SetVideoMode),
    ESP_ELFSYM_EXPORT(SDL_CreateRGBSurface),
    ESP_ELFSYM_EXPORT(SDL_FreeSurface),
    ESP_ELFSYM_EXPORT(SDL_Flip),
    ESP_ELFSYM_EXPORT(SDL_UpdateRect),
    ESP_ELFSYM_EXPORT(SDL_UpdateRects),
    ESP_ELFSYM_EXPORT(SDL_PollEvent),
    ESP_ELFSYM_EXPORT(SDL_WaitEvent),
    ESP_ELFSYM_EXPORT(SDL_PumpEvents),
    ESP_ELFSYM_EXPORT(SDL_GetMouseState),
    ESP_ELFSYM_EXPORT(SDL_GetTicks),
    ESP_ELFSYM_EXPORT(SDL_Delay),
    ESP_ELFSYM_EXPORT(SDL_SetError),

    // SDL 2.0 window & renderer
    ESP_ELFSYM_EXPORT(SDL_CreateWindow),
    ESP_ELFSYM_EXPORT(SDL_DestroyWindow),
    ESP_ELFSYM_EXPORT(SDL_CreateRenderer),
    ESP_ELFSYM_EXPORT(SDL_DestroyRenderer),
    ESP_ELFSYM_EXPORT(SDL_CreateTexture),
    ESP_ELFSYM_EXPORT(SDL_DestroyTexture),
    ESP_ELFSYM_EXPORT(SDL_UpdateTexture),
    ESP_ELFSYM_EXPORT(SDL_QueryTexture),
    ESP_ELFSYM_EXPORT(SDL_RenderClear),
    ESP_ELFSYM_EXPORT(SDL_RenderCopy),
    ESP_ELFSYM_EXPORT(SDL_RenderPresent),
    ESP_ELFSYM_END
};

int eos_elf_init_symbols(void) {
  int ret = esp_elf_register_symbol(g_eos_posix_elfsyms);
  if (ret != 0 && ret != -EEXIST) {
    EOS_LOGE("Failed to register POSIX symbol table: %d", ret);
    return ret;
  }

  ret = esp_elf_register_symbol(g_eos_sdl_elfsyms);
  if (ret != 0 && ret != -EEXIST) {
    EOS_LOGE("Failed to register SDL symbol table: %d", ret);
    return ret;
  }

  EOS_LOGI("Dynamic ELF loader symbol tables registered (POSIX + SDL)");
  return 0;
}
