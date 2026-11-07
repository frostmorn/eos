#include "ecore/proc.h"
#include "emisc/fancymacro.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// Wrappers for ESP-IDF heap_caps CORE functions to track ALL allocations in process context
// By wrapping ONLY heap_caps_aligned_alloc_base, we catch ALL memory allocations:
// - Standard malloc/free/calloc/realloc (they call heap_caps functions which eventually call aligned_alloc_base)
// - All heap_caps public functions (heap_caps_malloc, etc.)
// - All heap_caps prefer functions (heap_caps_malloc_prefer, etc.)  
// - All heap_caps aligned functions (heap_caps_aligned_alloc, etc.)

extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
extern void *__real_heap_caps_realloc_base(void *ptr, size_t size, uint32_t caps);
extern void __real_heap_caps_free(void *ptr);

static inline bool is_in_heap_wrap(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  uintptr_t flags = (uintptr_t)pvTaskGetThreadLocalStoragePointer(NULL, 1);
  return (flags & 2) != 0;
}

static inline void set_in_heap_wrap(bool val) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return;
  }
  uintptr_t flags = (uintptr_t)pvTaskGetThreadLocalStoragePointer(NULL, 1);
  if (val) {
    flags |= 2;
  } else {
    flags &= ~2;
  }
  vTaskSetThreadLocalStoragePointer(NULL, 1, (void *)flags);
}

void *__wrap_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps) {
  if (is_in_heap_wrap()) {
    return __real_heap_caps_aligned_alloc_base(alignment, size, caps);
  }
  set_in_heap_wrap(true);

  eos_proc_t *proc = eos_proc_current();
  
  void *ptr = __real_heap_caps_aligned_alloc_base(alignment, size, caps);
  if (ptr && proc) {
    pthread_mutex_lock(&proc->mem_mutex);
    kv_push(void *, proc->memblocks, ptr);
    pthread_mutex_unlock(&proc->mem_mutex);
  } else if (!ptr) {
    EOS_LOGE("heap_caps_aligned_alloc_base(%zu, %zu, %lu) failed\n", alignment, size, (unsigned long)caps);
  }

  set_in_heap_wrap(false);
  return ptr;
}

void *__wrap_heap_caps_realloc_base(void *ptr, size_t size, uint32_t caps) {
  if (is_in_heap_wrap()) {
    return __real_heap_caps_realloc_base(ptr, size, caps);
  }
  set_in_heap_wrap(true);

  eos_proc_t *proc = eos_proc_current();

  void *new_ptr = __real_heap_caps_realloc_base(ptr, size, caps);

  if (ptr && new_ptr && new_ptr != ptr && proc) {
    pthread_mutex_lock(&proc->mem_mutex);
    for (size_t i = 0; i < kv_size(proc->memblocks); i++) {
      if (kv_A(proc->memblocks, i) == ptr) {
        kv_A(proc->memblocks, i) = new_ptr;
        break;
      }
    }
    pthread_mutex_unlock(&proc->mem_mutex);
  }

  set_in_heap_wrap(false);
  return new_ptr;
}

void __wrap_heap_caps_free(void *ptr) {
  if (is_in_heap_wrap()) {
    __real_heap_caps_free(ptr);
    return;
  }
  set_in_heap_wrap(true);

  eos_proc_t *proc = eos_proc_current();
  
  if (ptr && proc) {
    pthread_mutex_lock(&proc->mem_mutex);
    for (size_t i = 0; i < kv_size(proc->memblocks); i++) {
      if (kv_A(proc->memblocks, i) == ptr) {
        kv_drop_fast(void *, proc->memblocks, i);
        break;
      }
    }
    pthread_mutex_unlock(&proc->mem_mutex);
  }
  
  __real_heap_caps_free(ptr);
  set_in_heap_wrap(false);
}
