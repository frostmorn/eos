#pragma once

/* The MIT License

   Copyright (c) 2008, by Attractive Chaos <attractor@live.co.uk>

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

/*
  An example:

#include "kvec.h"
int main() {
        kvec_t(int) array;
        kv_init(array);
        kv_push(int, array, 10); // append
        kv_a(int, array, 20) = 5; // dynamic
        kv_A(array, 20) = 4; // static
        kv_destroy(array);
        return 0;
}
*/

/*
  2008-09-22 (0.1.0):

        * The initial version.

*/

#ifndef AC_KVEC_H
#define AC_KVEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emisc/psram_alloc.h"

#define EOS_KVEC_HAS_HEAP_CAPS 1

// Zero-sized compile-time tag types
typedef struct {} __kvec_normal_tag_t;
typedef struct {} __kvec_psram_tag_t;

#define kv_roundup32(x)                                                        \
  (--(x), (x) |= (x) >> 1, (x) |= (x) >> 2, (x) |= (x) >> 4, (x) |= (x) >> 8,  \
   (x) |= (x) >> 16, ++(x))

#define kvec_t(type)                                                           \
  struct {                                                                     \
    size_t n, m;                                                               \
    type *a;                                                                   \
    uint32_t caps;                                                             \
    __kvec_normal_tag_t _tag;                                                  \
  }

#define kvec_psram_t(type)                                                     \
  struct {                                                                     \
    size_t n, m;                                                               \
    type *a;                                                                   \
    uint32_t caps;                                                             \
    __kvec_psram_tag_t _tag;                                                   \
  }

#define kv_is_psram(v) \
  __builtin_types_compatible_p(__typeof__((v)._tag), __kvec_psram_tag_t)

static inline void *eos_kvec_realloc(void *ptr, size_t size, uint32_t caps) {
#if defined(EOS_KVEC_HAS_HEAP_CAPS)
#  if defined(CONFIG_SPIRAM) || defined(CONFIG_ESP32_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S3_SPIRAM_SUPPORT)
  if (caps & MALLOC_CAP_SPIRAM) {
    return heap_caps_realloc_prefer(ptr, size, 2, caps, MALLOC_CAP_DEFAULT);
  }
#  endif
  return heap_caps_realloc(ptr, size, caps ? caps : MALLOC_CAP_DEFAULT);
#else
  (void)caps;
  return realloc(ptr, size);
#endif
}

#define kv_init(v)                                                             \
  ((v).n = (v).m = 0, (v).a = 0,                                               \
   (v).caps = kv_is_psram(v) ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)           \
                             : MALLOC_CAP_DEFAULT)

#define kv_init_psram(v)                                                       \
  ((v).n = (v).m = 0, (v).a = 0,                                               \
   (v).caps = (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))

#define kv_init_caps(v, c)                                                     \
  ((v).n = (v).m = 0, (v).a = 0, (v).caps = (c))

#define kv_destroy(v) free((v).a)
#define kv_A(v, i) ((v).a[(i)])
#define kv_pop(v) ((v).a[--(v).n])
#define kv_size(v) ((v).n)
#define kv_max(v) ((v).m)

#define kv_resize(type, v, s)                                                  \
  ((v).m = (s),                                                                \
   (v).a = (type *)eos_kvec_realloc((v).a, sizeof(type) * (v).m, (v).caps))

#define kv_copy(type, v1, v0)                                                  \
  do {                                                                         \
    if ((v1).m < (v0).n)                                                       \
      kv_resize(type, v1, (v0).n);                                             \
    (v1).n = (v0).n;                                                           \
    memcpy((v1).a, (v0).a, sizeof(type) * (v0).n);                             \
  } while (0)

#define kv_push(type, v, x)                                                    \
  do {                                                                         \
    if ((v).n == (v).m) {                                                      \
      (v).m = (v).m ? (v).m << 1 : 2;                                          \
      (v).a = (type *)eos_kvec_realloc((v).a, sizeof(type) * (v).m, (v).caps); \
    }                                                                          \
    (v).a[(v).n++] = (x);                                                      \
  } while (0)

#define kv_pushp(type, v)                                                      \
  (((v).n == (v).m)                                                            \
       ? ((v).m = ((v).m ? (v).m << 1 : 2),                                    \
          (v).a = (type *)eos_kvec_realloc((v).a, sizeof(type) * (v).m,       \
                                           (v).caps),                          \
          0)                                                                   \
       : 0),                                                                   \
      ((v).a + ((v).n++))

#define kv_a(type, v, i)                                                       \
  (((v).m <= (size_t)(i)                                                       \
        ? ((v).m = (v).n = (i) + 1, kv_roundup32((v).m),                       \
           (v).a = (type *)eos_kvec_realloc((v).a, sizeof(type) * (v).m,       \
                                            (v).caps),                         \
           0)                                                                  \
        : (v).n <= (size_t)(i) ? (v).n = (i) + 1                               \
                               : 0),                                           \
   (v).a[(i)])

#define kvec_drop(type, v, i)                                                  \
  do {                                                                         \
    memmove((v).a + (i), (v).a + (i) + 1, sizeof(type) * ((v).n - (i) - 1));   \
    --(v).n;                                                                   \
  } while (0)

#define kv_drop_fast(type, v, i)                                               \
  do {                                                                         \
    (v).a[(i)] = (v).a[--(v).n];                                               \
  } while (0)

#define kv_opt(type, v)                                                        \
  do {                                                                         \
    if ((v).n * 3 > (v).m) {                                                   \
      (v).m = (v).n * 2;                                                       \
      (v).a = (type *)eos_kvec_realloc((v).a, sizeof(type) * (v).m, (v).caps); \
    }                                                                          \
  } while (0)

#define kv_shrink(type, v)                                                     \
  do {                                                                         \
    (v).m = (v).n;                                                             \
    (v).a = (type *)eos_kvec_realloc((v).a, sizeof(type) * (v).m, (v).caps); \
  } while (0)

#endif
