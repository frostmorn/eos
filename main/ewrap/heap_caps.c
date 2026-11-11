#include "ecore/proc.h"
#include "emisc/fancymacro.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// Wrappers for ESP-IDF heap_caps CORE functions to track ALL allocations in process context
// - Standard malloc/free/calloc/realloc
// - All heap_caps public functions (heap_caps_malloc, etc.)
// - All heap_caps prefer functions (heap_caps_malloc_prefer, etc.)  
// - All heap_caps aligned functions (heap_caps_aligned_alloc, etc.)

extern void *__real_heap_caps_malloc_base(size_t size, uint32_t caps);
extern void *__real_heap_caps_calloc_base(size_t n, size_t size, uint32_t caps);
extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
extern void *__real_heap_caps_realloc_base(void *ptr, size_t size, uint32_t caps);
extern void __real_heap_caps_free(void *ptr);

static inline bool is_lwip_task(void) {
  return eos_proc_task_is_lwip();
}

static inline bool is_system_task(void) {
  return eos_proc_task_is_system();
}

void *__wrap_heap_caps_malloc_base(size_t size, uint32_t caps) {
  if (is_lwip_task() && !(caps & (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA))) {
    void *ptr = __real_heap_caps_malloc_base(size, caps | MALLOC_CAP_SPIRAM);
    if (ptr) return ptr;
    return __real_heap_caps_malloc_base(size, caps);
  }
  if (is_system_task() || eos_proc_in_heap_wrap() || eos_proc_heap_tracking_is_disabled()) {
    return __real_heap_caps_malloc_base(size, caps);
  }
  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    return __real_heap_caps_malloc_base(size, caps);
  }

  eos_proc_set_in_heap_wrap(true);

  void *ptr = __real_heap_caps_malloc_base(size, caps);
  if (ptr) {
    bool is_psram = (caps & MALLOC_CAP_SPIRAM) != 0;
    eos_proc_mem_insert(proc, ptr, size, is_psram);
  } else {
    EOS_LOGE("heap_caps_malloc_base(%zu, %lu) failed\n", size, (unsigned long)caps);
  }

  eos_proc_set_in_heap_wrap(false);
  return ptr;
}

void *__wrap_heap_caps_calloc_base(size_t n, size_t size, uint32_t caps) {
  if (is_lwip_task() && !(caps & (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA))) {
    void *ptr = __real_heap_caps_calloc_base(n, size, caps | MALLOC_CAP_SPIRAM);
    if (ptr) return ptr;
    return __real_heap_caps_calloc_base(n, size, caps);
  }
  if (is_system_task() || eos_proc_in_heap_wrap() || eos_proc_heap_tracking_is_disabled()) {
    return __real_heap_caps_calloc_base(n, size, caps);
  }
  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    return __real_heap_caps_calloc_base(n, size, caps);
  }

  eos_proc_set_in_heap_wrap(true);

  void *ptr = __real_heap_caps_calloc_base(n, size, caps);
  if (ptr) {
    bool is_psram = (caps & MALLOC_CAP_SPIRAM) != 0;
    eos_proc_mem_insert(proc, ptr, n * size, is_psram);
  } else {
    EOS_LOGE("heap_caps_calloc_base(%zu, %zu, %lu) failed\n", n, size, (unsigned long)caps);
  }

  eos_proc_set_in_heap_wrap(false);
  return ptr;
}

void *__wrap_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps) {
  if (is_lwip_task() && !(caps & (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA))) {
    void *ptr = __real_heap_caps_aligned_alloc_base(alignment, size, caps | MALLOC_CAP_SPIRAM);
    if (ptr) return ptr;
    return __real_heap_caps_aligned_alloc_base(alignment, size, caps);
  }
  if (is_system_task() || eos_proc_in_heap_wrap() || eos_proc_heap_tracking_is_disabled()) {
    return __real_heap_caps_aligned_alloc_base(alignment, size, caps);
  }
  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    return __real_heap_caps_aligned_alloc_base(alignment, size, caps);
  }

  eos_proc_set_in_heap_wrap(true);

  void *ptr = __real_heap_caps_aligned_alloc_base(alignment, size, caps);
  if (ptr) {
    bool is_psram = (caps & MALLOC_CAP_SPIRAM) != 0;
    eos_proc_mem_insert(proc, ptr, size, is_psram);
  } else {
    EOS_LOGE("heap_caps_aligned_alloc_base(%zu, %zu, %lu) failed\n", alignment, size, (unsigned long)caps);
  }

  eos_proc_set_in_heap_wrap(false);
  return ptr;
}

void *__wrap_heap_caps_realloc_base(void *ptr, size_t size, uint32_t caps) {
  if (is_lwip_task() && !(caps & (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA))) {
    void *new_ptr = __real_heap_caps_realloc_base(ptr, size, caps | MALLOC_CAP_SPIRAM);
    if (new_ptr) return new_ptr;
    return __real_heap_caps_realloc_base(ptr, size, caps);
  }
  if (is_system_task() || eos_proc_in_heap_wrap() || eos_proc_heap_tracking_is_disabled()) {
    return __real_heap_caps_realloc_base(ptr, size, caps);
  }
  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    return __real_heap_caps_realloc_base(ptr, size, caps);
  }

  eos_proc_set_in_heap_wrap(true);

  void *new_ptr = __real_heap_caps_realloc_base(ptr, size, caps);

  if (new_ptr) {
    bool is_psram = (caps & MALLOC_CAP_SPIRAM) != 0;
    eos_proc_mem_realloc(proc, ptr, new_ptr, size, is_psram);
  }

  eos_proc_set_in_heap_wrap(false);
  return new_ptr;
}

void __wrap_heap_caps_free(void *ptr) {
  if (!ptr) return;
  if (eos_proc_in_heap_wrap()) {
    __real_heap_caps_free(ptr);
    return;
  }

  eos_proc_set_in_heap_wrap(true);
  eos_proc_t *proc = eos_proc_current();
  eos_proc_mem_remove(proc, ptr);
  __real_heap_caps_free(ptr);
  eos_proc_set_in_heap_wrap(false);
}
