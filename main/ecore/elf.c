#include "ecore/elf.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"
#include "emisc/strlimits.h"
#include <errno.h>
#include <esp_heap_caps.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_elf.h"

#define ELF_MIN_HEADER_SIZE 52U
#define ELF_MAX_FILE_SIZE   (16U * 1024U * 1024U)

bool eos_is_elf_stream(FILE *f) {
  if (!f) return false;

  uint32_t magic = 0;
  if (fread(&magic, sizeof(magic), 1, f) == 1 && magic == EOS_ELF_MAGIC) {
    rewind(f);
    return true;
  }
  rewind(f);
  return false;
}

bool eos_is_elf_file(const char *path) {
  if (!path) return false;

  FILE *f = fopen(path, "rb");
  if (!f) return false;

  bool res = eos_is_elf_stream(f);
  fclose(f);
  return res;
}

int eos_elf_exec(const char *path, int argc, char **argv) {
  if (!path || argc < 0) {
    errno = EINVAL;
    return -1;
  }

  FILE *f = fopen(path, "rb");
  char resolved[EOS_MID_STR_LEN];
  if (!f && path[0] != '/' && !strchr(path, '/')) {
    // Try resolving bare command via PATH
    char *path_env = getenv("PATH");
    if (!path_env || !path_env[0]) {
      path_env = "/bin";
    }
    char path_buf[EOS_MID_STR_LEN];
    strlcpy(path_buf, path_env, sizeof(path_buf));
    char *saveptr = NULL;
    char *dir = strtok_r(path_buf, ":", &saveptr);
    while (dir) {
      snprintf(resolved, sizeof(resolved), "%s/%s", dir, path);
      f = fopen(resolved, "rb");
      if (f) {
        path = resolved;
        break;
      }
      dir = strtok_r(NULL, ":", &saveptr);
    }
  }

  if (!f) {
    EOS_LOGE("eos_elf_exec: cannot open '%s': %s", path, strerror(errno));
    return -1;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    errno = EIO;
    return -1;
  }

  long fsize = ftell(f);
  rewind(f);

  if (fsize < (long)ELF_MIN_HEADER_SIZE || fsize > (long)ELF_MAX_FILE_SIZE) {
    fclose(f);
    EOS_LOGE("eos_elf_exec: invalid ELF file size (%ld bytes) for '%s'", fsize, path);
    errno = ENOEXEC;
    return -1;
  }

  uint32_t magic = 0;
  if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != EOS_ELF_MAGIC) {
    fclose(f);
    EOS_LOGE("eos_elf_exec: file '%s' is not an ELF binary (magic=0x%08lX)",
             path, (unsigned long)magic);
    errno = ENOEXEC;
    return -1;
  }
  rewind(f);

  size_t alloc_size = (size_t)fsize;
  // esp_elf_relocate requires a 4-byte aligned payload buffer
  uint8_t *payload = (uint8_t *)heap_caps_aligned_alloc(
      4, alloc_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!payload) {
    payload = (uint8_t *)heap_caps_aligned_alloc(4, alloc_size, MALLOC_CAP_8BIT);
  }
  if (!payload) {
    fclose(f);
    EOS_LOGE("eos_elf_exec: failed to allocate %zu bytes for '%s'", alloc_size, path);
    errno = ENOMEM;
    return -1;
  }

  size_t read_bytes = fread(payload, 1, alloc_size, f);
  fclose(f);

  if (read_bytes != alloc_size) {
    heap_caps_free(payload);
    EOS_LOGE("eos_elf_exec: short read (%zu / %zu bytes) on '%s'",
             read_bytes, alloc_size, path);
    errno = EIO;
    return -1;
  }

  esp_elf_t elf;
  int ret = esp_elf_init(&elf);
  if (ret != 0) {
    heap_caps_free(payload);
    EOS_LOGE("eos_elf_exec: esp_elf_init failed (ret=%d) for '%s'", ret, path);
    errno = ENOMEM;
    return -1;
  }

  ret = esp_elf_relocate(&elf, payload, alloc_size);
  if (ret != 0) {
    esp_elf_deinit(&elf);
    heap_caps_free(payload);
    EOS_LOGE("eos_elf_exec: esp_elf_relocate failed (ret=%d) for '%s' — unresolved symbols or incompatible arch",
             ret, path);
    errno = ENOEXEC;
    return -1;
  }

  fflush(stdout);
  fflush(stderr);

  int exit_status = esp_elf_request(&elf, 0, argc, argv);

  fflush(stdout);
  fflush(stderr);

  esp_elf_deinit(&elf);
  heap_caps_free(payload);

  return exit_status;
}

int eos_elf_stage_entry(int argc, char **argv) {
  if (argc <= 0 || !argv || !argv[0]) {
    return -1;
  }

  return eos_elf_exec(argv[0], argc, argv);
}
