#include "ecore/bin.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifndef EOS_PTHREAD_MIN_STACK_SIZE
#define EOS_PTHREAD_MIN_STACK_SIZE EOS_BIN_MIN_STACK_SIZE
#endif

extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*start_routine)(void *), void *arg);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg) {

  eos_twrap_t *twrap = eos_twrap_prepare(start_routine, arg);
  if (!twrap) {
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

  EOS_LOGI("Launching new thread\n");
  // Launch thread
  int rc = __real_pthread_create(thread, use_attr, eos_twrap_pthread, twrap);

  if (destroy_local) {
    pthread_attr_destroy(&local_attr);
  }

  if (rc != 0) {
    if (twrap->env_copy) {
      for (size_t i = 0; i < twrap->env_count; i++) {
        free(twrap->env_copy[i]);
      }
      free(twrap->env_copy);
    }
    free(twrap);
  }

  return rc;
}
