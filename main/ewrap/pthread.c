#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*start_routine)(void *), void *arg);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg) {

  eos_twrap_t *twrap = eos_twrap_prepare(start_routine, arg);
  if (!twrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return -1;
  }

  EOS_LOGI("Launching new thread\n");
  // Launch thread
  int rc = __real_pthread_create(thread, attr, eos_twrap_pthread, twrap);

  if (rc != 0) {
    free(twrap);
  }

  return rc;
}
