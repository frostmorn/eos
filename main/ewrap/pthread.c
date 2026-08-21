#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*start_routine)(void *), void *arg);

typedef struct {
  void *(*real_start_routine)(void *);
  void *real_arg;
  char inherited_cwd[PATH_MAX];
} eos_pthread_trampoline_ctx_t;


static void *eos_pthread_trampoline(void *raw) {
  eos_pthread_trampoline_ctx_t *tramp = (eos_pthread_trampoline_ctx_t *)raw;

  // Grab what we need out of the smuggled struct before freeing it
  void *(*real_start_routine)(void *) = tramp->real_start_routine;
  void *real_arg = tramp->real_arg;

  eos_tctx_t *tctx = eos_tctx_alloc();
  if (tctx) {
    strcpy(tctx->cwd, tramp->inherited_cwd);
    eos_tctx_set(tctx);
  } else {

    EOS_LOGE("Failed to allocate thread context for new thread - "
             "cwd tracking will not work on this thread\n");
    abort();
  }

  free(tramp);

  return real_start_routine(real_arg);
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg) {

  EOS_LOGI("New thread has been started\n");

  // Snapshot the CREATING thread's current cwd now, on its own stack,
  // before the new thread exists at all - by the time the new thread
  // actually runs, the creator may already have moved on / changed
  // its own cwd, so this has to happen here, not inside the trampoline.
  eos_tctx_t *cur_tctx = eos_tctx_get();

  eos_pthread_trampoline_ctx_t *tramp =
      malloc(sizeof(eos_pthread_trampoline_ctx_t));
  if (!tramp) {
    EOS_LOGE("Failed to allocate trampoline context - falling back to "
             "uninherited thread creation\n");
    return __real_pthread_create(thread, attr, start_routine, arg);
  }

  tramp->real_start_routine = start_routine;
  tramp->real_arg = arg;

  if (cur_tctx) {
    strcpy(tramp->inherited_cwd, cur_tctx->cwd);
  } else {
    // Creator has no context of its own either - fall back to root
    // rather than propagate garbage.
    strcpy(tramp->inherited_cwd, "/");
  }

  int rc = __real_pthread_create(thread, attr, eos_pthread_trampoline, tramp);

  if (rc != 0) {
    // Thread creation itself failed - the trampoline never ran, so
    // nobody else will ever free this. We still own it.
    free(tramp);
  }

  return rc;
}