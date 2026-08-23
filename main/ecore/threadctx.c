#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

static bool tctx_init_done = false;

// This requires system wide TLS INDEX register
#define EOS_TCTX_TLS_INDEX 1 // 0 is reserved
#if CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS <= EOS_TCTX_TLS_INDEX
#error                                                                         \
    "Not enough FreeRTOS TLS pointers configured for eos_tctx(CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS)"
#endif

eos_tctx_t *eos_tctx_get(void) {
  if (!tctx_init_done)
    return NULL;
  return (eos_tctx_t *)pvTaskGetThreadLocalStoragePointer(NULL,
                                                          EOS_TCTX_TLS_INDEX);
}

// idk what x here is but hope I got it right
static void eos_tctx_tls_delete(int x, void *ptr) {
  if (x == EOS_TCTX_TLS_INDEX)
    eos_tctx_free((eos_tctx_t *)ptr);
}

void eos_tctx_set(eos_tctx_t *tctx) {
  vTaskSetThreadLocalStoragePointerAndDelCallback(
      NULL, EOS_TCTX_TLS_INDEX, tctx,
      (TlsDeleteCallbackFunction_t)eos_tctx_tls_delete);
}

eos_tctx_t *eos_tctx_alloc() {
  eos_tctx_t *tctx = malloc(sizeof(eos_tctx_t));

  if (!tctx) {
    EOS_LOGE("Can't allocate new thread context. Not enough memory?\n");
    return NULL;
  }

  memset(tctx, 0, sizeof(eos_tctx_t));

  return tctx;
}

void eos_tctx_free(eos_tctx_t *tctx) {
  if (!tctx) {
    EOS_LOGW("Trying to free empty thread context\n");
    return;
  }

  EOS_LOGI("Cleaning up thread context\n");

  // Active file descriptors:
  if (tctx->fds) {
    if (tctx->fds_count) {
      EOS_LOGW("Found %d file descriptors in use\n", tctx->fds_count);
      for (size_t i = 0; i < tctx->fds_count; i++) {
        close(tctx->fds[i]);
      }
    }
    free(tctx->fds);
  }
  // Open Directories:
  if (tctx->dirs) {
    if (tctx->dirs_count) {
      EOS_LOGW("Found %d open directories\n", tctx->dirs_count);
      for (size_t i = 0; i < tctx->dirs_count; i++) {
        closedir(tctx->dirs[i]); // recursion :D
      }
    }
    free(tctx->dirs);
  }
  // Non freed memory blocks
  if (tctx->memblocks) {
    if (tctx->memblocks_count) {
      EOS_LOGW("Found %d not freed memory blocks\n", tctx->memblocks_count);
      for (size_t i = 0; i < tctx->memblocks_count; i++) {
        free(tctx->memblocks[i]);
      }
    }
    free(tctx->memblocks);
  }

  free(tctx);
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
    tctx->dirs = malloc(sizeof(int) * 2);
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
  }
}

void eos_tctx_reg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx) {}

void eos_tctx_unreg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx) {}

void eos_tctx_init(void) {
  if (!tctx_init_done) {
    if (eos_tctx_get())
      return;

    eos_tctx_t *tctx = eos_tctx_alloc();
    if (!tctx) {
      EOS_LOGE("Can't initialize thread context\n");
      return;
    }

    strcpy(tctx->cwd, "/");

    eos_tctx_set(tctx);
    tctx_init_done = true;
  }
}

/// => thread wrap
eos_twrap_t *eos_twrap_prepare(void *thread_start, void *thread_data) {
  eos_tctx_t *cur_tctx = eos_tctx_get();

  // Allocate twrap data
  eos_twrap_t *twrap = malloc(sizeof(eos_twrap_t));

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

  // Cleanup
  free(twrap);

  return thread_start(thread_arg);
}

// come on
// void eos_twrap_freertos(void *data) { eos_twrap_pthread(data); }