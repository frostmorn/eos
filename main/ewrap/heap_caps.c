#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

// Wrappers for ESP-IDF heap_caps CORE functions to track ALL allocations in thread context
// By wrapping ONLY heap_caps_aligned_alloc_base, we catch ALL memory allocations:
// - Standard malloc/free/calloc/realloc (they call heap_caps functions which eventually call aligned_alloc_base)
// - All heap_caps public functions (heap_caps_malloc, etc.)
// - All heap_caps prefer functions (heap_caps_malloc_prefer, etc.)  
// - All heap_caps aligned functions (heap_caps_aligned_alloc, etc.)
//
// The hierarchy is:
// heap_caps_calloc_base -> heap_caps_malloc_base -> heap_caps_aligned_alloc_base
// heap_caps_malloc_base -> heap_caps_aligned_alloc_base
// heap_caps_realloc_base -> heap_caps_aligned_alloc_base
//
// So wrapping heap_caps_aligned_alloc_base catches ALL allocations!

// External declarations of real ESP-IDF functions
extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
extern void __real_heap_caps_free(void *ptr);

// Wrapper for heap_caps_aligned_alloc_base - catches ALL memory allocations
void *__wrap_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps) {
  // Get thread context once - don't call eos_tctx_get() after this to avoid recursion
  eos_tctx_t *tctx = eos_tctx_get();
  
  void *ptr = __real_heap_caps_aligned_alloc_base(alignment, size, caps);
  if (ptr && tctx) {
    // Inline registration to avoid recursion with kv_opt
    kv_push(void *, tctx->memblocks, ptr);
  } else if (!ptr) {
    EOS_LOGE("heap_caps_aligned_alloc_base(%zu, %zu, %lu) failed\n", alignment, size, (unsigned long)caps);
  }
  return ptr;
}

// Wrapper for heap_caps_free - catches ALL memory deallocations
void __wrap_heap_caps_free(void *ptr) {
  // Get thread context once - don't call eos_tctx_get() after this to avoid recursion
  eos_tctx_t *tctx = eos_tctx_get();
  
  // Unregister before freeing - only if block is actually in our tracking list
  if (ptr && tctx) {
    // Inline check: only unregister if ptr is in the vector
    // This avoids recursion issues with kv_opt calling realloc
    for (size_t i = 0; i < kv_size(tctx->memblocks); i++) {
      if (kv_A(tctx->memblocks, i) == ptr) {
        kv_drop_fast(void *, tctx->memblocks, i);
        kv_opt(void *, tctx->memblocks);
        break;
      }
    }
  }
  
  __real_heap_caps_free(ptr);
}