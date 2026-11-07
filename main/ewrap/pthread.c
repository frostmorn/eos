#include "ecore/bin.h"
#include "ecore/proc.h"
#include "emisc/fancymacro.h"
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifndef EOS_PTHREAD_MIN_STACK_SIZE
#define EOS_PTHREAD_MIN_STACK_SIZE EOS_BIN_MIN_STACK_SIZE
#endif

extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*start_routine)(void *), void *arg);

void eos_pthread_set_stack_cfg(uint32_t stack_size, bool prefer_psram,
                               const char *name) {
  esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
  if (stack_size >= EOS_BIN_MIN_STACK_SIZE) {
    cfg.stack_size = stack_size;
  }
  if (prefer_psram) {
    cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  } else {
    cfg.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  }
  cfg.inherit_cfg = false;
  if (name && name[0] != '\0') {
    cfg.thread_name = name;
  }
  esp_pthread_set_cfg(&cfg);
}

void eos_pthread_set_manifest_cfg(const eos_bin_t *bin) {
  if (!bin) {
    eos_pthread_set_stack_cfg(EOS_BIN_DEFAULT_STACK_SIZE, false, NULL);
    return;
  }
  uint32_t stack_size = eos_bin_effective_stack_size(bin);
  bool prefer_psram = eos_bin_prefers_psram_stack(bin);
  const char *name = (bin->filename[0] != '\0') ? bin->filename : NULL;
  eos_pthread_set_stack_cfg(stack_size, prefer_psram, name);
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg) {

  eos_pwrap_t *pwrap = eos_proc_wrap_prepare(start_routine, arg);
  if (!pwrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return -1;
  }

  pthread_attr_t local_attr;
  const pthread_attr_t *use_attr = attr;
  bool destroy_local = false;

  if (!attr) {
    if (pthread_attr_init(&local_attr) == 0) {
      pthread_attr_setstacksize(&local_attr, EOS_BIN_DEFAULT_STACK_SIZE);
      use_attr = &local_attr;
      destroy_local = true;
    }
  } else {
    size_t stack_size = 0;
    if (pthread_attr_getstacksize(attr, &stack_size) == 0 &&
        stack_size < EOS_PTHREAD_MIN_STACK_SIZE) {
      local_attr = *attr;
      pthread_attr_setstacksize(&local_attr, EOS_PTHREAD_MIN_STACK_SIZE);
      use_attr = &local_attr;
    }
  }

  // Launch thread
  int rc = __real_pthread_create(thread, use_attr, eos_proc_wrap_pthread, pwrap);

  // If PSRAM allocation failed, fallback gracefully to internal SRAM
  if (rc != 0) {
    esp_pthread_cfg_t cur_cfg;
    if (esp_pthread_get_cfg(&cur_cfg) == ESP_OK &&
        (cur_cfg.stack_alloc_caps & MALLOC_CAP_SPIRAM)) {
      EOS_LOGW("PSRAM stack allocation failed (%d); falling back to internal SRAM\n", rc);
      cur_cfg.stack_alloc_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
      esp_pthread_set_cfg(&cur_cfg);
      rc = __real_pthread_create(thread, use_attr, eos_proc_wrap_pthread, pwrap);
    }
  }

  // Restore default pthread configuration on calling thread
  esp_pthread_cfg_t default_cfg = esp_pthread_get_default_config();
  esp_pthread_set_cfg(&default_cfg);

  if (destroy_local) {
    pthread_attr_destroy(&local_attr);
  }

  if (rc != 0) {
    free(pwrap);
  }

  return rc;
}
