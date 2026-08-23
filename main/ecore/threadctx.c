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

  return tctx;
}

static void eos_tctx_free(eos_tctx_t *tctx) {
  if (!tctx) {
    EOS_LOGW("Trying to free empty thread context\n");
    return;
  }

  EOS_LOGI("Cleaning up thread context\n");

  // Active file descriptors:
  if (tctx->fds)
    free(tctx->fds);

  // Open Directories:
  if (tctx->dirs)
    free(tctx->dirs);
  // Non freed memory blocks
  if (tctx->memblocks)
    free(tctx->memblocks);

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

  // Check if memory allocated for fd storage
  if (!tctx->fds) {
    tctx->fds = malloc(sizeof(int) * 2);
    if (!tctx->fds) {
      EOS_LOGE("Memory allocation for thread context fds failed\n");
      return;
    }
    tctx->fds_count = 0;
    tctx->fds_cap = 2;
    // Fill with bad fds
    tctx->fds[0] = -1;
    tctx->fds[1] = -1;
  }

  // Allocate additional memory if needed
  if (tctx->fds_cap == tctx->fds_count) {
    size_t newcap = tctx->fds_cap * 2;
    // RESERARCH: how reallocarray() would work with PSRAM blocks
    void *newblock = reallocarray(tctx->fds, newcap, sizeof(int));
    if (!newblock) {
      EOS_LOGE("Memory allocation failed for thread context fds storage\n");
      return;
    }
    // Swap blocks
    tctx->fds = newblock;
    tctx->fds_cap = newcap;

    // Fill with bad fds
    for (size_t i = tctx->fds_count; i < newcap; i++) {
      tctx->fds[i] = -1;
    }
  }

  // Register
  tctx->fds[tctx->fds_count] = fd;
  tctx->fds_count++;
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
  int *pfd = NULL;
  size_t i;
  if ((tctx->fds && tctx->fds_count)) {
    for (i = 0; i < tctx->fds_count; i++) {
      if (tctx->fds[i] == fd) {
        pfd = &(tctx->fds[i]);
        break;
      }
    }
  } // oh my god

  // Check if fd found
  if (!pfd) {
    EOS_LOGE("fd %d not found. impossible to unregister\n", fd);
    return;
  }

  // Handle special case
  if (tctx->fds_count == 1) {
    tctx->fds_count = 0;
    tctx->fds_cap = 0;
    free(tctx->fds);
    tctx->fds = NULL;
    return;
  }

  // Unregister and compact list
  tctx->fds_count--;
  tctx->fds[i] = tctx->fds[tctx->fds_count];
  tctx->fds[tctx->fds_count] = -1;

  // Shrink mem if needed
  if (tctx->fds_count * 3 < tctx->fds_cap) {
    size_t newcap = tctx->fds_cap / 2;
    if (newcap < 2)
      newcap = 2;
    // Probably we can use just realloc here, but well, who cares
    void *newblock = reallocarray(tctx->fds, newcap, sizeof(int));
    if (!newblock) {
      EOS_LOGE("Memory allocation failed for thread context fds storage\n");
      return;
    }
    tctx->fds = newblock;
    tctx->fds_cap = newcap;
  }
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

  // Check if memory allocated for dir storage
  if (!tctx->dirs) {
    tctx->dirs = malloc(sizeof(DIR *) * 2);
    if (!tctx->dirs) {
      EOS_LOGE("Memory allocation for thread context dirs failed\n");
      return;
    }
    tctx->dirs_count = 0;
    tctx->dirs_cap = 2;
    // FIll with bad dirs
    tctx->dirs[0] = NULL;
    tctx->dirs[1] = NULL;
  }

  // Allocate additional memory if needed
  if (tctx->dirs_cap == tctx->dirs_count) {
    size_t newcap = tctx->dirs_cap * 2;
    // RESERARCH: how reallocarray() would work with PSRAM blocks
    void *newblock = reallocarray(tctx->dirs, newcap, sizeof(DIR *));
    if (!newblock) {
      EOS_LOGE("Memory allocation failed for thread context dirs storage\n");
      return;
    }
    // Swap blocks
    tctx->dirs = newblock;
    tctx->dirs_cap = newcap;

    // Fill with bad dirs
    for (size_t i = tctx->dirs_count; i < newcap; i++) {
      tctx->dirs[i] = NULL;
    }
  }

  // Register
  tctx->dirs[tctx->dirs_count] = dir;
  tctx->dirs_count++;
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

  // Find dir slot
  DIR **ppdir = NULL;
  size_t i;
  if ((tctx->dirs && tctx->dirs_count)) {
    for (i = 0; i < tctx->dirs_count; i++) {
      if (tctx->dirs[i] == dir) {
        ppdir = &(tctx->dirs[i]);
        break;
      }
    }
  } // oh my god

  // Check if dir found
  if (!ppdir) {
    EOS_LOGE("dir %p not found. impossible to unregister\n", dir);
    return;
  }

  // Handle special case
  if (tctx->dirs_count == 1) {
    tctx->dirs_count = 0;
    tctx->dirs_cap = 0;
    free(tctx->dirs);
    tctx->dirs = NULL;
    return;
  }

  // Unregister and compact list
  tctx->dirs_count--;
  tctx->dirs[i] = tctx->dirs[tctx->dirs_count];
  tctx->dirs[tctx->dirs_count] = NULL;

  // Shrink mem if needed
  if (tctx->dirs_count * 3 < tctx->dirs_cap) {
    size_t newcap = tctx->dirs_cap / 2;
    if (newcap < 2)
      newcap = 2;
    // Probably we can use just realloc here, but well, who cares
    void *newblock = reallocarray(tctx->dirs, newcap, sizeof(DIR *));
    if (!newblock) {
      EOS_LOGE("Memory allocation failed for thread context dir storage\n");
      return;
    }
    tctx->dirs = newblock;
    tctx->dirs_cap = newcap;
  }
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
  while (tctx->fds_count) {
    EOS_LOGW("Closing leaked fd %d\n", tctx->fds[0]);
    close(tctx->fds[0]);
  }

  while (tctx->dirs_count) {
    EOS_LOGW("Closing leaked dir %p\n", tctx->dirs[0]);
    closedir(tctx->dirs[0]);
  }

  // Cleanup context
  eos_tctx_free(tctx);

  return thread_result;
}

void eos_twrap_freertos(void *data) {
  // here we can just call a pthread version, since it's identical
  eos_twrap_pthread(data);
}