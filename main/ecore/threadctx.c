#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

// This requires system wide TLS INDEX register
#define EOS_TCTX_TLS_INDEX 1 // 0 is reserved
#if CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS <= EOS_TCTX_TLS_INDEX
#error                                                                         \
    "Not enough FreeRTOS TLS pointers configured for eos_tctx(CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS)"
#endif

static eos_tctx_t *eos_tctx_alloc() {
  eos_tctx_t *tctx = malloc(sizeof(eos_tctx_t));

  if (!tctx) {
    EOS_LOGE("Can't allocate new thread context. Not enough memory?\n");
    return NULL;
  }

  memset(tctx, 0, sizeof(eos_tctx_t));

  kv_init(tctx->fds);
  kv_init(tctx->dirs);
  kv_init(tctx->memblocks);

  return tctx;
}

static void eos_tctx_free(eos_tctx_t *tctx) {
  if (!tctx) {
    EOS_LOGW("Trying to free empty thread context\n");
    return;
  }

  EOS_LOGI("Cleaning up thread context\n");

  // Cleanup
  kv_destroy(tctx->fds);
  kv_destroy(tctx->dirs);
  kv_destroy(tctx->memblocks);

  free(tctx);
}

static void eos_tctx_set(eos_tctx_t *tctx) {
  vTaskSetThreadLocalStoragePointer(NULL, EOS_TCTX_TLS_INDEX, tctx);
}

eos_tctx_t *eos_tctx_get(void) {
  TaskHandle_t task = xTaskGetCurrentTaskHandle();

  // insane, but, we can't get our thread local storage pointer in case
  // we do not really have even a first thread :D
  // yeah yeah, this can happen for real
  if (task == NULL) {
    return NULL;
  }

  eos_tctx_t *tctx =
      pvTaskGetThreadLocalStoragePointer(NULL, EOS_TCTX_TLS_INDEX);

  // can be not initialized yet
  if (!tctx) {
    tctx = eos_tctx_alloc();
    strcpy(tctx->cwd, "/");
  }

  if (!tctx) {
    EOS_LOGE("Can't initialize thread context\n");
    abort();
  }

  eos_tctx_set(tctx);

  return tctx;
}

void eos_tctx_reg_fd(int fd, eos_tctx_t *tctx) {
  // Check context
  if (!tctx) {
    EOS_LOGW("Trying to register file descriptor for empty thread context\n");
    return;
  }
  // Check fd
  if (fd < 0) {
    EOS_LOGE("Trying to register bad fd %d within thread context\n", fd);
    return;
  }

  // Register
  kv_push(int, tctx->fds, fd);
}

void eos_tctx_unreg_fd(int fd, eos_tctx_t *tctx) {
  // Check context
  if (!tctx) {
    EOS_LOGW("Trying to unregister file descriptor for empty thread context\n");
    return;
  }
  // Check fd
  if (fd < 0) {
    EOS_LOGE("Trying to unregister bad fd %d within thread context\n", fd);
    return;
  }

  // Find fd slot
  bool found = false;
  size_t i;
  for (i = 0; i < kv_size(tctx->fds); i++) {
    if (kv_A(tctx->fds, i) == fd) {
      found = true;
      break;
    }
  }
  // Check if fd found
  if (!found) {
    EOS_LOGE("Can't unreg fd %d\n", fd);
  }
  // Unregister and compact list
  kvec_drop_fast(int, tctx->fds, i);
  kvec_opt(int, tctx->fds);
}

// Almost exact copy of tctx_reg_fd
// if do changes here, apply them in both places
void eos_tctx_reg_dir(DIR *dir, eos_tctx_t *tctx) {
  // Check context
  if (!tctx) {
    EOS_LOGW("Trying to register dir for empty thread context\n");
    return;
  }
  // Check dir
  if (!dir) {
    EOS_LOGE("Trying to register dir %p within thread context\n", dir);
    return;
  }

  // Register
  kv_push(DIR *, tctx->dirs, dir);
}

// Almost exact copy of tctx_unreg_fd
// if do changes here, apply them in both places
void eos_tctx_unreg_dir(DIR *dir, eos_tctx_t *tctx) {
  // Check context
  if (!tctx) {
    EOS_LOGW("Trying to unregister dir for empty thread context\n");
    return;
  }
  // Check dir
  if (!dir) {
    EOS_LOGE("Trying to unregister bad dir %p within thread context\n", dir);
    return;
  }

  // Find fd slot
  bool found = false;
  size_t i;
  for (i = 0; i < kv_size(tctx->dirs); i++) {
    if (kv_A(tctx->dirs, i) == dir) {
      found = true;
      break;
    }
  }
  // Check if fd found
  if (!found) {
    EOS_LOGE("Can't unreg dir %p\n", dir);
  }
  // Unregister and compact list
  kvec_drop_fast(DIR *, tctx->dirs, i);
  kvec_opt(DIR *, tctx->dirs);
}

void eos_tctx_reg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx) {}

void eos_tctx_unreg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx) {}

/// => thread wrap
eos_twrap_t *eos_twrap_prepare(void *thread_start, void *thread_data) {
  eos_tctx_t *cur_tctx = eos_tctx_get();

  // Allocate twrap data
  eos_twrap_t *twrap = malloc(sizeof(eos_twrap_t));

  if (!twrap) {
    EOS_LOGE("twrap_prepare failed\n");
    return NULL;
  }

  // Fill twrap data
  twrap->thread_start = thread_start;
  twrap->thread_arg = thread_data;

  if (cur_tctx) {
    strcpy(twrap->cwd, cur_tctx->cwd);
  } else {
    strcpy(twrap->cwd, "/");
  }

  return twrap;
}

void *eos_twrap_pthread(void *data) {
  eos_twrap_t *twrap = data;

  if (!twrap) {
    EOS_LOGE("Can't launch thread. Wrap data invalid\n");
    return NULL;
  }

  // Store thread launch params
  void *(*thread_start)(void *) = twrap->thread_start;
  void *thread_arg = twrap->thread_arg;

  // Initializing new thread context
  eos_tctx_t *tctx = eos_tctx_alloc();
  if (!tctx) {
    EOS_LOGE("Failed to allocate thread context\n");
    return NULL;
  }
  // Copy inherit data
  strcpy(tctx->cwd, twrap->cwd);
  eos_tctx_set(tctx);

  // Cleanup twrap
  free(twrap);

  void *thread_result = thread_start(thread_arg);

  // TODO: RTOS task never reach that since is destroyed via vTaskDelete(NULL)

  // Cleanup resources. Note it won't happen in case thread killed from outside
  while (kv_size(tctx->fds)) {
    EOS_LOGW("Closing leaked fd %d\n", kv_A(tctx->fds, 0));
    close(kv_A(tctx->fds, 0));
  }

  while (kv_size(tctx->dirs)) {
    EOS_LOGW("Closing leaked fd %p\n", kv_A(tctx->dirs, 0));
    closedir(kv_A(tctx->dirs, 0));
  }

  // Cleanup context
  eos_tctx_free(tctx);

  return thread_result;
}

void eos_twrap_freertos(void *data) {
  // here we can just call a pthread version, since it's identical
  eos_twrap_pthread(data);
}