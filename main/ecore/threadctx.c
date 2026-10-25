#include "ecore/threadctx.h"
#include "ecore/bin.h"
#include "ecore/log.h"
#include "emisc/fancymacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <esp_heap_caps.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// This requires system wide TLS INDEX register
#define EOS_TCTX_TLS_INDEX 1 // 0 is reserved
#if CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS <= EOS_TCTX_TLS_INDEX
#error                                                                         \
    "Not enough FreeRTOS TLS pointers configured for eos_tctx(CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS)"
#endif

static void eos_tctx_set(eos_tctx_t *tctx);

static eos_tctx_t *eos_tctx_alloc() {
  // Use heap_caps directly to avoid recursion with our wrappers
  // MALLOC_CAP_DEFAULT allows allocation from internal heap or SPIRAM
  // IMPORTANT: Must use __real_ heap_caps_aligned_alloc_base to avoid recursion with our wrappers
  extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
  eos_tctx_t *tctx = __real_heap_caps_aligned_alloc_base(4, sizeof(eos_tctx_t), MALLOC_CAP_DEFAULT);

  if (!tctx) {
    EOS_LOGE("Can't allocate new thread context. Not enough memory?\n");
    return NULL;
  }

  memset(tctx, 0, sizeof(eos_tctx_t));

  kv_init(tctx->fds);
  kv_init(tctx->dirs);
  kv_init(tctx->memblocks);
  kv_init(tctx->env);
  tctx->env_array = NULL;
  tctx->last_exit_status = 0;

  for (int i = 0; i < FD_SETSIZE; i++) {
    tctx->fd_map[i] = -1;
  }

  // Set TLS pointer early so recursive calls to eos_tctx_get() find this tctx during initialization
  eos_tctx_set(tctx);

  eos_tctx_init_env(tctx);

  return tctx;
}

static void eos_tctx_rebuild_env_array(eos_tctx_t *tctx) {
  extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
  extern void __real_heap_caps_free(void *ptr);

  if (tctx->env_array) {
    __real_heap_caps_free(tctx->env_array);
    tctx->env_array = NULL;
  }

  size_t n = kv_size(tctx->env);
  tctx->env_array = __real_heap_caps_aligned_alloc_base(4, (n + 1) * sizeof(char *), MALLOC_CAP_DEFAULT);
  if (tctx->env_array) {
    for (size_t i = 0; i < n; i++) {
      tctx->env_array[i] = kv_A(tctx->env, i);
    }
    tctx->env_array[n] = NULL;
  }
}

static int eos_tctx_setenv_internal(eos_tctx_t *tctx, const char *name, const char *value, int overwrite) {
  if (!tctx || !name || !value) return -1;

  extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
  extern void __real_heap_caps_free(void *ptr);

  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(tctx->env); i++) {
    char *entry = kv_A(tctx->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      if (!overwrite) return 0;
      size_t new_len = name_len + 1 + strlen(value) + 1;
      char *new_entry = __real_heap_caps_aligned_alloc_base(4, new_len, MALLOC_CAP_DEFAULT);
      if (!new_entry) return -1;
      snprintf(new_entry, new_len, "%s=%s", name, value);
      __real_heap_caps_free(entry);
      kv_A(tctx->env, i) = new_entry;
      eos_tctx_rebuild_env_array(tctx);
      return 0;
    }
  }

  size_t new_len = name_len + 1 + strlen(value) + 1;
  char *new_entry = __real_heap_caps_aligned_alloc_base(4, new_len, MALLOC_CAP_DEFAULT);
  if (!new_entry) return -1;
  snprintf(new_entry, new_len, "%s=%s", name, value);
  kv_push(char *, tctx->env, new_entry);
  eos_tctx_rebuild_env_array(tctx);
  return 0;
}

void eos_tctx_init_env(eos_tctx_t *tctx) {
  if (!tctx) return;
  eos_tctx_setenv_internal(tctx, "PATH", "/bin", 1);
  eos_tctx_setenv_internal(tctx, "HOME", "/", 1);
  eos_tctx_setenv_internal(tctx, "USER", "root", 1);
  eos_tctx_setenv_internal(tctx, "SHELL", "/bin/ush", 1);
  eos_tctx_setenv_internal(tctx, "EOS_LOG", "0", 0);
  eos_tctx_setenv_internal(tctx, "EOS_LOG_FILE", EOS_DEFAULT_LOG_FILE, 0);
}

char *eos_tctx_getenv(const char *name) {
  eos_tctx_t *tctx = eos_tctx_get();
  if (!tctx || !name) return NULL;

  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(tctx->env); i++) {
    char *entry = kv_A(tctx->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      return entry + name_len + 1;
    }
  }
  return NULL;
}

int eos_tctx_setenv(const char *name, const char *value, int overwrite) {
  eos_tctx_t *tctx = eos_tctx_get();
  int rc = eos_tctx_setenv_internal(tctx, name, value, overwrite);
  if (rc == 0 && overwrite) {
    eos_log_sync_env(name, value);
  }
  return rc;
}

int eos_tctx_unsetenv(const char *name) {
  eos_tctx_t *tctx = eos_tctx_get();
  if (!tctx || !name) return -1;

  extern void __real_heap_caps_free(void *ptr);

  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(tctx->env); i++) {
    char *entry = kv_A(tctx->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      __real_heap_caps_free(entry);
      kv_drop_fast(char *, tctx->env, i);
      kv_opt(char *, tctx->env);
      eos_tctx_rebuild_env_array(tctx);
      eos_log_sync_env(name, NULL);
      return 0;
    }
  }
  eos_log_sync_env(name, NULL);
  return 0;
}

char **eos_tctx_get_environ(void) {
  eos_tctx_t *tctx = eos_tctx_get();
  if (!tctx) return NULL;
  return tctx->env_array;
}

static void eos_tctx_free(eos_tctx_t *tctx) {
  if (!tctx) {
    EOS_LOGW("Trying to free empty thread context\n");
    return;
  }

  EOS_LOGI("Cleaning up thread context\n");

  // Cleanup - use __real_heap_caps_free directly to avoid recursion with our wrappers
  kv_destroy(tctx->fds);
  kv_destroy(tctx->dirs);
  kv_destroy(tctx->memblocks);
  for (size_t i = 0; i < kv_size(tctx->env); i++) {
    if (kv_A(tctx->env, i)) {
      extern void __real_heap_caps_free(void *ptr);
      __real_heap_caps_free(kv_A(tctx->env, i));
    }
  }
  kv_destroy(tctx->env);
  if (tctx->env_array) {
    extern void __real_heap_caps_free(void *ptr);
    __real_heap_caps_free(tctx->env_array);
  }

  extern void __real_heap_caps_free(void *ptr);
  __real_heap_caps_free(tctx);
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
      (eos_tctx_t *)pvTaskGetThreadLocalStoragePointer(NULL, EOS_TCTX_TLS_INDEX);
  if (tctx) {
    return tctx;
  }

  // Internal ESP-IDF system tasks must not have thread context auto-allocated
  const char *name = pcTaskGetName(NULL);
  if (name && (strncmp(name, "ipc", 3) == 0 ||
               strncmp(name, "esp_", 4) == 0 ||
               strncmp(name, "IDLE", 4) == 0 ||
               strcmp(name, "tiT") == 0 ||
               strcmp(name, "sys_evt") == 0 ||
               strcmp(name, "wifi") == 0)) {
    return NULL;
  }

  // Guard against tasks with small stack headroom to prevent stack overflow
  UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(NULL);
  if (stack_hwm * sizeof(StackType_t) < EOS_BIN_MIN_STACK_SIZE) {
    return NULL;
  }

  tctx = eos_tctx_alloc();
  if (!tctx) {
    EOS_LOGE("Can't initialize thread context\n");
    return NULL;
  }
  strcpy(tctx->cwd, "/");
  eos_tctx_set(tctx);

  return tctx;
}

int eos_tctx_resolve_fd(int fd) {
  if (fd < 0 || fd >= FD_SETSIZE)
    return fd;

  eos_tctx_t *tctx = eos_tctx_get();
  if (!tctx)
    return fd;

  int mapped = tctx->fd_map[fd];
  return (mapped >= 0) ? mapped : fd;
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
    return;
  }
  // Unregister and compact list
  kv_drop_fast(int, tctx->fds, i);
  kv_opt(int, tctx->fds);
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
    return;
  }
  // Unregister and compact list
  kv_drop_fast(DIR *, tctx->dirs, i);
  kv_opt(DIR *, tctx->dirs);
}

eos_tctx_fd_snapshot_t eos_tctx_snapshot_fds(eos_tctx_t *tctx) {
  eos_tctx_fd_snapshot_t snap = {0};
  if (!tctx) return snap;

  size_t n_fds = kv_size(tctx->fds);
  if (n_fds > 0) {
    extern void *__real_heap_caps_aligned_alloc_base(size_t, size_t, uint32_t);
    snap.fds = __real_heap_caps_aligned_alloc_base(4, n_fds * sizeof(int), MALLOC_CAP_DEFAULT);
    if (snap.fds) {
      for (size_t j = 0; j < n_fds; j++) {
        snap.fds[j] = kv_A(tctx->fds, j);
      }
      snap.count = n_fds;
    }
  }

  size_t n_dirs = kv_size(tctx->dirs);
  if (n_dirs > 0) {
    extern void *__real_heap_caps_aligned_alloc_base(size_t, size_t, uint32_t);
    snap.dirs = __real_heap_caps_aligned_alloc_base(4, n_dirs * sizeof(DIR *), MALLOC_CAP_DEFAULT);
    if (snap.dirs) {
      for (size_t j = 0; j < n_dirs; j++) {
        snap.dirs[j] = kv_A(tctx->dirs, j);
      }
      snap.dir_count = n_dirs;
    }
  }

  return snap;
}

void eos_tctx_cleanup_leaked_fds(eos_tctx_t *tctx, eos_tctx_fd_snapshot_t *snap) {
  if (!tctx) {
    if (snap) {
      extern void __real_heap_caps_free(void *ptr);
      if (snap->fds) __real_heap_caps_free(snap->fds);
      if (snap->dirs) __real_heap_caps_free(snap->dirs);
      snap->fds = NULL;
      snap->dirs = NULL;
      snap->count = 0;
      snap->dir_count = 0;
    }
    return;
  }

  // 1. Close any leaked file descriptors not in the snapshot
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < kv_size(tctx->fds); i++) {
      int fd = kv_A(tctx->fds, i);
      bool was_present = false;
      if (snap && snap->fds) {
        for (size_t j = 0; j < snap->count; j++) {
          if (snap->fds[j] == fd) {
            was_present = true;
            break;
          }
        }
      }
      if (!was_present) {
        EOS_LOGW("Closing leaked fd %d\n", fd);
        close(fd);
        changed = true;
        break; // restart search since close() mutates tctx->fds
      }
    }
  }

  // 2. Close any leaked directory handles not in the snapshot
  changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < kv_size(tctx->dirs); i++) {
      DIR *dir = kv_A(tctx->dirs, i);
      bool was_present = false;
      if (snap && snap->dirs) {
        for (size_t j = 0; j < snap->dir_count; j++) {
          if (snap->dirs[j] == dir) {
            was_present = true;
            break;
          }
        }
      }
      if (!was_present) {
        EOS_LOGW("Closing leaked dir %p\n", dir);
        closedir(dir);
        changed = true;
        break; // restart search since closedir() mutates tctx->dirs
      }
    }
  }

  // 3. Free snapshot buffers
  if (snap) {
    extern void __real_heap_caps_free(void *ptr);
    if (snap->fds) {
      __real_heap_caps_free(snap->fds);
      snap->fds = NULL;
    }
    if (snap->dirs) {
      __real_heap_caps_free(snap->dirs);
      snap->dirs = NULL;
    }
    snap->count = 0;
    snap->dir_count = 0;
  }
}

void eos_tctx_reg_memblock(void *block, eos_tctx_t *tctx) {
  if (!tctx || !block) return;
  kv_push(void *, tctx->memblocks, block);
}

void eos_tctx_unreg_memblock(void *block, eos_tctx_t *tctx) {
  if (!tctx || !block) return;
  for (size_t i = 0; i < kv_size(tctx->memblocks); i++) {
    if (kv_A(tctx->memblocks, i) == block) {
      kv_drop_fast(void *, tctx->memblocks, i);
      kv_opt(void *, tctx->memblocks);
      break;
    }
  }
}

/// => thread wrap
eos_twrap_t *eos_twrap_prepare(void *thread_start, void *thread_data) {
  eos_tctx_t *cur_tctx = eos_tctx_get();

  // Allocate twrap data
  eos_twrap_t *twrap = malloc(sizeof(eos_twrap_t));

  if (!twrap) {
    EOS_LOGE("twrap_prepare failed\n");
    return NULL;
  }

  memset(twrap, 0, sizeof(eos_twrap_t));

  // twrap is freed by the child thread in eos_twrap_pthread; unregister from parent tctx
  if (cur_tctx) {
    eos_tctx_unreg_memblock(twrap, cur_tctx);
  }

  // Fill twrap data
  twrap->thread_start = thread_start;
  twrap->thread_arg = thread_data;

  if (cur_tctx) {
    strcpy(twrap->cwd, cur_tctx->cwd);
    for (int i = 0; i < 3; i++) {
      twrap->stdio_fds[i] = cur_tctx->fd_map[i];
    }

    size_t env_n = kv_size(cur_tctx->env);
    if (env_n > 0) {
      twrap->env_copy = calloc(env_n, sizeof(char *));
      if (twrap->env_copy) {
        eos_tctx_unreg_memblock(twrap->env_copy, cur_tctx);
        for (size_t i = 0; i < env_n; i++) {
          const char *e = kv_A(cur_tctx->env, i);
          if (e) {
            char *dup_e = strdup(e);
            if (dup_e) {
              eos_tctx_unreg_memblock(dup_e, cur_tctx);
              twrap->env_copy[twrap->env_count++] = dup_e;
            }
          }
        }
      }
    }
  } else {
    strcpy(twrap->cwd, "/");
    for (int i = 0; i < 3; i++) {
      twrap->stdio_fds[i] = -1;
    }
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
    if (twrap->env_copy) {
      for (size_t i = 0; i < twrap->env_count; i++) {
        free(twrap->env_copy[i]);
      }
      free(twrap->env_copy);
    }
    free(twrap);
    return NULL;
  }
  // Copy inherit data
  strcpy(tctx->cwd, twrap->cwd);
  for (int i = 0; i < 3; i++) {
    tctx->fd_map[i] = twrap->stdio_fds[i];
  }
  if (twrap->env_copy) {
    for (size_t i = 0; i < twrap->env_count; i++) {
      char *entry = twrap->env_copy[i];
      if (entry) {
        char *eq = strchr(entry, '=');
        if (eq) {
          *eq = '\0';
          eos_tctx_setenv_internal(tctx, entry, eq + 1, 1);
        }
        free(entry);
      }
    }
    free(twrap->env_copy);
    twrap->env_copy = NULL;
  }
  eos_tctx_set(tctx);

  // Cleanup twrap
  free(twrap);

  void *thread_result = thread_start(thread_arg);

  // Flush any buffered thread output while inherited stdio fd_map is still active
  fflush(stdout);
  fflush(stderr);

  // Cleanup resources. Note it won't happen in case thread killed from outside
  while (kv_size(tctx->fds)) {
    EOS_LOGW("Closing leaked fd %d\n", kv_A(tctx->fds, 0));
    close(kv_A(tctx->fds, 0));
  }

  while (kv_size(tctx->dirs)) {
    EOS_LOGW("Closing leaked dir %p\n", kv_A(tctx->dirs, 0));
    closedir(kv_A(tctx->dirs, 0));
  }

  // Free any leaked memory blocks
  // Use __real_heap_caps_free directly to avoid recursion through our wrappers
  while (kv_size(tctx->memblocks)) {
    void *ptr = kv_A(tctx->memblocks, 0);
    EOS_LOGW("Freeing leaked mem block %p\n", ptr);
    // Remove from vector and free - no need to unregister since we're cleaning up
    kv_drop_fast(void *, tctx->memblocks, 0);
    kv_opt(void *, tctx->memblocks);
    extern void __real_heap_caps_free(void *ptr);
    __real_heap_caps_free(ptr);
  }

  // Clear TLS pointer before freeing context to prevent dangling access
  eos_tctx_set(NULL);
  // Cleanup context
  eos_tctx_free(tctx);

  return thread_result;
}

void eos_twrap_freertos(void *data) {
  eos_twrap_pthread(data);
  vTaskDelete(NULL);
}