#include <pthread.h>
#include "emisc/fancymacro.h"

extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);

extern int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg){

  EOS_LOGI("New thread has been started\n");
  return __real_pthread_create(thread, attr, start_routine, arg);
}

