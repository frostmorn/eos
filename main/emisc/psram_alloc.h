#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include(<esp_heap_caps.h>)
#    include <esp_heap_caps.h>
#    define EOS_HAS_HEAP_CAPS 1
#  endif
#endif

#ifndef MALLOC_CAP_DEFAULT
#define MALLOC_CAP_DEFAULT  (1<<0)
#endif
#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM   (1<<10)
#endif
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT     (1<<2)
#endif

/**
 * @brief Allocate memory preferring external PSRAM (SPIRAM) with fallback to internal SRAM.
 */
static inline void *eos_psram_malloc_prefer(size_t size) {
#if defined(EOS_HAS_HEAP_CAPS) && (defined(CONFIG_SPIRAM) || defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S3_SPIRAM_SUPPORT))
  return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
#else
  return malloc(size);
#endif
}

/**
 * @brief Zero-allocate memory preferring external PSRAM (SPIRAM) with fallback to internal SRAM.
 */
static inline void *eos_psram_calloc_prefer(size_t n, size_t size) {
#if defined(EOS_HAS_HEAP_CAPS) && (defined(CONFIG_SPIRAM) || defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S3_SPIRAM_SUPPORT))
  return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
#else
  return calloc(n, size);
#endif
}

/**
 * @brief Reallocate memory preferring external PSRAM (SPIRAM) with fallback to internal SRAM.
 */
static inline void *eos_psram_realloc_prefer(void *ptr, size_t size) {
#if defined(EOS_HAS_HEAP_CAPS) && (defined(CONFIG_SPIRAM) || defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S3_SPIRAM_SUPPORT))
  return heap_caps_realloc_prefer(ptr, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
#else
  return realloc(ptr, size);
#endif
}

/**
 * @brief Duplicate string preferring external PSRAM (SPIRAM) with fallback to internal SRAM.
 */
static inline char *eos_psram_strdup(const char *s) {
  if (!s) return NULL;
  size_t len = strlen(s) + 1;
  char *d = (char *)eos_psram_malloc_prefer(len);
  if (d) {
    memcpy(d, s, len);
  }
  return d;
}

/**
 * @brief Free memory allocated by eos_psram_* functions.
 */
static inline void eos_psram_free(void *ptr) {
  free(ptr);
}
