#include "ecore/proc.h"
#include "ecore/bin.h"
#include "ecore/log.h"
#include "ecore/vfs.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <esp_timer.h>
#include <esp_pthread.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reent.h>
#include <unistd.h>

static eos_proc_t *s_proc_table = NULL;
static pthread_mutex_t s_proc_mutex;
static bool s_proc_mutex_inited = false;
static pthread_cond_t s_proc_table_cond;
static pid_t s_next_pid = 2;

static inline void proc_lock(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || !s_proc_mutex_inited) {
    return;
  }
  pthread_mutex_lock(&s_proc_mutex);
}

static inline void proc_unlock(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || !s_proc_mutex_inited) {
    return;
  }
  pthread_mutex_unlock(&s_proc_mutex);
}

static bool proc_memblock_remove_locked(eos_proc_t *proc, void *ptr, size_t *out_size, bool *out_is_psram);

extern void eos_log_sync_env(const char *name, const char *value);

static void eos_proc_rebuild_env_array(eos_proc_t *proc) {
  bool prev_h = eos_proc_heap_tracking_disable();
  extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
  extern void __real_heap_caps_free(void *ptr);

  if (proc->env_array) {
    __real_heap_caps_free(proc->env_array);
    proc->env_array = NULL;
  }

  size_t n = kv_size(proc->env);
  proc->env_array = __real_heap_caps_aligned_alloc_base(4, (n + 1) * sizeof(char *), MALLOC_CAP_DEFAULT);
  if (proc->env_array) {
    for (size_t i = 0; i < n; i++) {
      proc->env_array[i] = kv_A(proc->env, i);
    }
    proc->env_array[n] = NULL;
  }
  eos_proc_heap_tracking_restore(prev_h);
}

static int eos_proc_setenv_internal(eos_proc_t *proc, const char *name, const char *value, int overwrite) {
  if (!proc || !name || !value) return -1;
  bool prev_h = eos_proc_heap_tracking_disable();

  extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
  extern void __real_heap_caps_free(void *ptr);

  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(proc->env); i++) {
    char *entry = kv_A(proc->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      if (!overwrite) {
        eos_proc_heap_tracking_restore(prev_h);
        return 0;
      }
      size_t new_len = name_len + 1 + strlen(value) + 1;
      char *new_entry = __real_heap_caps_aligned_alloc_base(4, new_len, MALLOC_CAP_DEFAULT);
      if (!new_entry) {
        eos_proc_heap_tracking_restore(prev_h);
        return -1;
      }
      snprintf(new_entry, new_len, "%s=%s", name, value);
      __real_heap_caps_free(entry);
      kv_A(proc->env, i) = new_entry;
      eos_proc_rebuild_env_array(proc);
      eos_proc_heap_tracking_restore(prev_h);
      return 0;
    }
  }

  size_t new_len = name_len + 1 + strlen(value) + 1;
  char *new_entry = __real_heap_caps_aligned_alloc_base(4, new_len, MALLOC_CAP_DEFAULT);
  if (!new_entry) {
    eos_proc_heap_tracking_restore(prev_h);
    return -1;
  }
  snprintf(new_entry, new_len, "%s=%s", name, value);
  kv_push(char *, proc->env, new_entry);
  eos_proc_rebuild_env_array(proc);
  eos_proc_heap_tracking_restore(prev_h);
  return 0;
}

void eos_proc_init_env(eos_proc_t *proc) {
  if (!proc) return;
  eos_proc_setenv_internal(proc, "PATH", "/bin", 1);
  eos_proc_setenv_internal(proc, "HOME", "/", 1);
  eos_proc_setenv_internal(proc, "USER", "root", 1);
  eos_proc_setenv_internal(proc, "SHELL", "/bin/ush", 1);
  eos_proc_setenv_internal(proc, "EOS_LOG", "0", 0);
  eos_proc_setenv_internal(proc, "EOS_LOG_FILE", EOS_DEFAULT_LOG_FILE, 0);
}

char *eos_proc_getenv(const char *name) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc || !name) return NULL;

  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(proc->env); i++) {
    char *entry = kv_A(proc->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      return entry + name_len + 1;
    }
  }
  return NULL;
}

int eos_proc_setenv(const char *name, const char *value, int overwrite) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return -1;
  int rc = eos_proc_setenv_internal(proc, name, value, overwrite);
  if (rc == 0 && overwrite) {
    eos_log_sync_env(name, value);
  }
  return rc;
}

int eos_proc_unsetenv(const char *name) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc || !name) return -1;

  extern void __real_heap_caps_free(void *ptr);

  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(proc->env); i++) {
    char *entry = kv_A(proc->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      __real_heap_caps_free(entry);
      kv_drop_fast(char *, proc->env, i);
      kv_opt(char *, proc->env);
      eos_proc_rebuild_env_array(proc);
      eos_log_sync_env(name, NULL);
      return 0;
    }
  }
  eos_log_sync_env(name, NULL);
  return 0;
}

char **eos_proc_get_environ(void) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return NULL;
  return proc->env_array;
}

static pid_t alloc_next_pid_locked(void) {
  for (int attempt = 0; attempt < 32767; attempt++) {
    pid_t cand = s_next_pid++;
    if (s_next_pid > 32767) {
      s_next_pid = 2;
    }
    if (cand < 2) {
      cand = 2;
    }

    bool collision = false;
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use && s_proc_table[i].pid == cand) {
        collision = true;
        break;
      }
    }
    if (!collision) {
      return cand;
    }
  }
  return -1;
}

#define EOS_MAX_TASK_FLAGS 64

typedef struct {
  TaskHandle_t task;
  uint32_t flags;
} eos_task_flags_t;

static eos_task_flags_t *s_task_flags = NULL;
static portMUX_TYPE s_task_flags_spinlock = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_task_flags_active_count = 0;

static eos_proc_t *s_kernel_proc = NULL;

static inline void task_flags_lock(void) {
  portENTER_CRITICAL(&s_task_flags_spinlock);
}

static inline void task_flags_unlock(void) {
  portEXIT_CRITICAL(&s_task_flags_spinlock);
}

static inline uint32_t task_get_kernel_depth(uint32_t flags) {
  return (flags >> EOS_TASK_KERNEL_DEPTH_SHIFT) & EOS_TASK_KERNEL_DEPTH_MASK;
}

static inline uint32_t task_set_kernel_depth(uint32_t flags, uint32_t depth) {
  return (flags & ~(EOS_TASK_KERNEL_DEPTH_MASK << EOS_TASK_KERNEL_DEPTH_SHIFT)) |
         ((depth & EOS_TASK_KERNEL_DEPTH_MASK) << EOS_TASK_KERNEL_DEPTH_SHIFT);
}

static inline uint32_t task_get_heap_dis_depth(uint32_t flags) {
  return (flags >> EOS_TASK_HEAP_DIS_SHIFT) & EOS_TASK_HEAP_DIS_MASK;
}

static inline uint32_t task_set_heap_dis_depth(uint32_t flags, uint32_t depth) {
  return (flags & ~(EOS_TASK_HEAP_DIS_MASK << EOS_TASK_HEAP_DIS_SHIFT)) |
         ((depth & EOS_TASK_HEAP_DIS_MASK) << EOS_TASK_HEAP_DIS_SHIFT);
}

static void classify_task_locked(eos_task_flags_t *entry, TaskHandle_t task, const char *hint_name) {
  if (entry->flags & EOS_TASK_FLAG_CLASSIFIED) return;
  const char *name = hint_name ? hint_name : pcTaskGetName(task);
  if (name) {
    if (strcmp(name, "tiT") == 0 || strcmp(name, "tcpip") == 0) {
      entry->flags |= (EOS_TASK_FLAG_LWIP | EOS_TASK_FLAG_SYSTEM);
    } else if (strncmp(name, "IDLE", 4) == 0 ||
               strncmp(name, "ipc", 3) == 0 ||
               strncmp(name, "esp_", 4) == 0 ||
               strncmp(name, "sdio_", 5) == 0 ||
               strncmp(name, "rpc_", 4) == 0 ||
               strcmp(name, "Tmr Svc") == 0 ||
               strcmp(name, "sys_evt") == 0 ||
               strcmp(name, "wifi") == 0) {
      entry->flags |= EOS_TASK_FLAG_SYSTEM;
    }
  }
  entry->flags |= EOS_TASK_FLAG_CLASSIFIED;
}

static eos_task_flags_t *get_task_flags_locked(TaskHandle_t task, bool create) {
  if (!task || !s_task_flags) return NULL;
  int free_idx = -1;
  for (int i = 0; i < EOS_MAX_TASK_FLAGS; i++) {
    if (s_task_flags[i].task == task) {
      return &s_task_flags[i];
    }
    if (create && free_idx < 0 && s_task_flags[i].task == NULL) {
      free_idx = i;
    }
  }
  if (create && free_idx >= 0) {
    s_task_flags[free_idx].task = task;
    s_task_flags[free_idx].flags = 0;
    classify_task_locked(&s_task_flags[free_idx], task, NULL);
    s_task_flags_active_count++;
    return &s_task_flags[free_idx];
  }
  return NULL;
}

static void release_task_flags_locked(eos_task_flags_t *entry) {
  if (entry && entry->task && !(entry->flags & (EOS_TASK_FLAG_SYSTEM | EOS_TASK_FLAG_LWIP))) {
    if (task_get_kernel_depth(entry->flags) == 0 &&
        task_get_heap_dis_depth(entry->flags) == 0 &&
        !(entry->flags & EOS_TASK_FLAG_IN_HEAP_WRAP)) {
      entry->task = NULL;
      entry->flags = 0;
      if (s_task_flags_active_count > 0) {
        s_task_flags_active_count--;
      }
    }
  }
}

void eos_proc_task_classify(TaskHandle_t task, const char *name) {
  if (!task || !s_task_flags) return;
  task_flags_lock();
  eos_task_flags_t *entry = get_task_flags_locked(task, true);
  if (entry) {
    classify_task_locked(entry, task, name);
  }
  task_flags_unlock();
}

bool eos_proc_task_is_lwip(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return false;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return false;

  task_flags_lock();
  eos_task_flags_t *entry = get_task_flags_locked(task, true);
  bool ret = (entry != NULL && (entry->flags & EOS_TASK_FLAG_LWIP) != 0);
  task_flags_unlock();
  return ret;
}

bool eos_proc_task_is_system(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  task_flags_lock();
  eos_task_flags_t *entry = get_task_flags_locked(task, true);
  bool ret = (entry != NULL && (entry->flags & EOS_TASK_FLAG_SYSTEM) != 0);
  task_flags_unlock();
  return ret;
}

void eos_proc_task_flags_cleanup(void *task_ptr) {
  TaskHandle_t task = (TaskHandle_t)task_ptr;
  if (!task || !s_task_flags || s_task_flags_active_count == 0) return;
  task_flags_lock();
  if (s_task_flags) {
    for (int i = 0; i < EOS_MAX_TASK_FLAGS; i++) {
      if (s_task_flags[i].task == task) {
        memset(&s_task_flags[i], 0, sizeof(eos_task_flags_t));
        if (s_task_flags_active_count > 0) {
          s_task_flags_active_count--;
        }
        break;
      }
    }
  }
  task_flags_unlock();
}

bool eos_proc_enter_kernel(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  task_flags_lock();
  if (!s_task_flags) {
    task_flags_unlock();
    return true;
  }
  eos_task_flags_t *entry = get_task_flags_locked(task, true);
  uint32_t depth = entry ? task_get_kernel_depth(entry->flags) : 0;
  bool prev = (depth > 0);
  if (entry) {
    entry->flags = task_set_kernel_depth(entry->flags, depth + 1);
  }
  task_flags_unlock();
  return prev;
}

void eos_proc_leave_kernel(bool prev) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return;

  task_flags_lock();
  if (!s_task_flags) {
    task_flags_unlock();
    return;
  }
  eos_task_flags_t *entry = get_task_flags_locked(task, false);
  if (entry) {
    uint32_t depth = task_get_kernel_depth(entry->flags);
    if (depth > 0) {
      depth--;
    }
    if (!prev) {
      depth = 0;
    }
    entry->flags = task_set_kernel_depth(entry->flags, depth);
    release_task_flags_locked(entry);
  }
  task_flags_unlock();
}

bool eos_proc_is_kernel(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  if (!s_task_flags || s_task_flags_active_count == 0) {
    return false;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  task_flags_lock();
  eos_task_flags_t *entry = get_task_flags_locked(task, false);
  bool is_k = (entry != NULL && task_get_kernel_depth(entry->flags) > 0);
  task_flags_unlock();
  return is_k;
}

bool eos_proc_heap_tracking_disable(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  task_flags_lock();
  if (!s_task_flags) {
    task_flags_unlock();
    return true;
  }
  eos_task_flags_t *entry = get_task_flags_locked(task, true);
  uint32_t depth = entry ? task_get_heap_dis_depth(entry->flags) : 0;
  bool prev = (depth > 0);
  if (entry) {
    entry->flags = task_set_heap_dis_depth(entry->flags, depth + 1);
  }
  task_flags_unlock();
  return prev;
}

void eos_proc_heap_tracking_restore(bool prev) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return;

  task_flags_lock();
  if (!s_task_flags) {
    task_flags_unlock();
    return;
  }
  eos_task_flags_t *entry = get_task_flags_locked(task, false);
  if (entry) {
    uint32_t depth = task_get_heap_dis_depth(entry->flags);
    if (depth > 0) {
      depth--;
    }
    if (!prev) {
      depth = 0;
    }
    entry->flags = task_set_heap_dis_depth(entry->flags, depth);
    release_task_flags_locked(entry);
  }
  task_flags_unlock();
}

bool eos_proc_heap_tracking_is_disabled(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  if (!s_task_flags) {
    return true;
  }
  if (s_task_flags_active_count == 0) {
    return false;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  task_flags_lock();
  eos_task_flags_t *entry = get_task_flags_locked(task, false);
  bool disabled = (entry != NULL && task_get_heap_dis_depth(entry->flags) > 0);
  task_flags_unlock();
  return disabled;
}

bool eos_proc_in_heap_wrap(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  if (!s_task_flags || s_task_flags_active_count == 0) {
    return false;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  task_flags_lock();
  eos_task_flags_t *entry = get_task_flags_locked(task, false);
  bool ret = (entry != NULL && (entry->flags & EOS_TASK_FLAG_IN_HEAP_WRAP) != 0);
  task_flags_unlock();
  return ret;
}

void eos_proc_set_in_heap_wrap(bool val) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return;

  task_flags_lock();
  if (!s_task_flags) {
    task_flags_unlock();
    return;
  }
  eos_task_flags_t *entry = get_task_flags_locked(task, val);
  if (entry) {
    if (val) {
      entry->flags |= EOS_TASK_FLAG_IN_HEAP_WRAP;
    } else {
      entry->flags &= ~EOS_TASK_FLAG_IN_HEAP_WRAP;
    }
    release_task_flags_locked(entry);
  }
  task_flags_unlock();
}

eos_proc_t *eos_proc_current(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return s_kernel_proc;
  }

  if (eos_proc_is_kernel()) {
    return s_kernel_proc;
  }

  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (task == NULL) {
    return s_kernel_proc;
  }

  if (eos_proc_task_is_system()) {
    return NULL;
  }

  if (s_proc_table) {
    // 1. Check if this task is the main thread of an active process (reverse order for child priority)
    for (int i = (int)EOS_MAX_PROCS - 1; i >= 0; i--) {
      if (s_proc_table[i].in_use && s_proc_table[i].state != EOS_PROC_STATE_ZOMBIE &&
          s_proc_table[i].main_thread == task) {
        return &s_proc_table[i];
      }
    }

    // 2. Check if this task is registered in any process's threads list
    for (int i = (int)EOS_MAX_PROCS - 1; i >= 0; i--) {
      if (!s_proc_table[i].in_use || s_proc_table[i].state == EOS_PROC_STATE_ZOMBIE) continue;
      for (size_t j = 0; j < kv_size(s_proc_table[i].threads); j++) {
        if (kv_A(s_proc_table[i].threads, j) == task) {
          return &s_proc_table[i];
        }
      }
    }
  }

  return NULL;
}

void eos_proc_init(void) {
  task_flags_lock();
  if (!s_task_flags) {
    s_task_flags = (eos_task_flags_t *)heap_caps_calloc(EOS_MAX_TASK_FLAGS, sizeof(eos_task_flags_t), MALLOC_CAP_INTERNAL);
  }
  task_flags_unlock();

  if (!s_proc_mutex_inited) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_proc_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&s_proc_table_cond, NULL);
    s_proc_mutex_inited = true;
  }

  proc_lock();
  if (s_proc_table != NULL) {
    proc_unlock();
    return;
  }

  // Initialize PID 0 (kernel context)
  if (!s_kernel_proc) {
    s_kernel_proc = (eos_proc_t *)eos_psram_calloc_prefer(1, sizeof(eos_proc_t));
    if (!s_kernel_proc) {
      s_kernel_proc = (eos_proc_t *)calloc(1, sizeof(eos_proc_t));
    }
  }
  if (!s_kernel_proc) {
    EOS_LOGE("Failed to allocate kernel process context!\n");
    proc_unlock();
    return;
  }

  memset(s_kernel_proc, 0, sizeof(eos_proc_t));
  s_kernel_proc->pid = 0;
  s_kernel_proc->ppid = 0;
  s_kernel_proc->pgid = 0;
  s_kernel_proc->sid = 0;
  s_kernel_proc->state = EOS_PROC_STATE_RUNNING;
  s_kernel_proc->start_time = esp_timer_get_time();
  s_kernel_proc->in_use = true;
  strlcpy(s_kernel_proc->name, "kernel", sizeof(s_kernel_proc->name));
  strlcpy(s_kernel_proc->cwd, "/", sizeof(s_kernel_proc->cwd));
  strlcpy(s_kernel_proc->tty, "/dev/console", sizeof(s_kernel_proc->tty));

  kv_init(s_kernel_proc->fds);
  kv_init(s_kernel_proc->dirs);
  kv_init(s_kernel_proc->memblocks);
  kv_init(s_kernel_proc->env);
  kv_init(s_kernel_proc->threads);
  kv_init(s_kernel_proc->fd_map);
  pthread_mutexattr_t km_attr;
  pthread_mutexattr_init(&km_attr);
  pthread_mutexattr_settype(&km_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&s_kernel_proc->mem_mutex, &km_attr);
  pthread_mutexattr_destroy(&km_attr);
  pthread_cond_init(&s_kernel_proc->wait_cond, NULL);

  s_proc_table = (eos_proc_t *)eos_psram_calloc_prefer(EOS_MAX_PROCS, sizeof(eos_proc_t));
  if (!s_proc_table) {
    s_proc_table = (eos_proc_t *)calloc(EOS_MAX_PROCS, sizeof(eos_proc_t));
  }
  if (!s_proc_table) {
    EOS_LOGE("Failed to allocate process table!\n");
    proc_unlock();
    return;
  }

  pthread_mutexattr_t m_attr;
  pthread_mutexattr_init(&m_attr);
  pthread_mutexattr_settype(&m_attr, PTHREAD_MUTEX_RECURSIVE);
  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    pthread_mutex_init(&s_proc_table[i].mem_mutex, &m_attr);
    pthread_cond_init(&s_proc_table[i].wait_cond, NULL);
  }
  pthread_mutexattr_destroy(&m_attr);

  // Initialize PID 1 (init / ush shell)
  eos_proc_t *init_proc = &s_proc_table[0];
  init_proc->pid = 1;
  init_proc->ppid = 0;
  init_proc->pgid = 1;
  init_proc->sid = 1;
  init_proc->state = EOS_PROC_STATE_RUNNING;
  init_proc->start_time = esp_timer_get_time();
  init_proc->in_use = true;
  strlcpy(init_proc->name, "init", sizeof(init_proc->name));
  strlcpy(init_proc->cwd, "/", sizeof(init_proc->cwd));
  strlcpy(init_proc->tty, "/dev/console", sizeof(init_proc->tty));

  kv_init(init_proc->fds);
  kv_init(init_proc->dirs);
  kv_init(init_proc->memblocks);
  kv_init(init_proc->env);
  kv_init(init_proc->threads);
  kv_init(init_proc->fd_map);

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_proc_init_env(init_proc);

  init_proc->main_thread = xTaskGetCurrentTaskHandle();
  kv_push(TaskHandle_t, init_proc->threads, init_proc->main_thread);
  eos_proc_heap_tracking_restore(prev_h);

  proc_unlock();
  EOS_LOGI("Process table initialized (max %u procs in PSRAM)\n", (unsigned)EOS_MAX_PROCS);
}

pid_t eos_proc_create(const char *name, const char *cmdline, pid_t ppid, pid_t pgid) {
  if (!s_proc_table) {
    eos_proc_init();
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  proc_lock();

  int slot = -1;
  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    if (!s_proc_table[i].in_use) {
      slot = (int)i;
      break;
    }
  }

  if (slot < 0) {
    proc_unlock();
    eos_proc_heap_tracking_restore(prev_h);
    EOS_LOGE("Process table full (all %u slots in use)\n", (unsigned)EOS_MAX_PROCS);
    errno = EAGAIN;
    return -1;
  }

  pid_t new_pid = alloc_next_pid_locked();
  if (new_pid < 0) {
    proc_unlock();
    eos_proc_heap_tracking_restore(prev_h);
    errno = EAGAIN;
    return -1;
  }

  eos_proc_t *cur = eos_proc_current();
  eos_proc_t *p = &s_proc_table[slot];

  pthread_mutex_t saved_mem_mutex = p->mem_mutex;
  pthread_cond_t saved_wait_cond = p->wait_cond;
  memset(p, 0, sizeof(eos_proc_t));
  p->mem_mutex = saved_mem_mutex;
  p->wait_cond = saved_wait_cond;

  p->pid = new_pid;
  p->ppid = (ppid > 0) ? ppid : (cur ? cur->pid : 1);
  p->pgid = (pgid > 0) ? pgid : new_pid;
  p->sid = (cur ? cur->sid : new_pid);
  p->state = EOS_PROC_STATE_RUNNING;
  p->start_time = esp_timer_get_time();
  p->in_use = true;

  if (cur && cur->cwd[0] != '\0') {
    strlcpy(p->cwd, cur->cwd, sizeof(p->cwd));
  } else {
    strlcpy(p->cwd, "/", sizeof(p->cwd));
  }

  if (name) {
    const char *slash = strrchr(name, '/');
    strlcpy(p->name, slash ? slash + 1 : name, sizeof(p->name));
  } else {
    strlcpy(p->name, "proc", sizeof(p->name));
  }

  if (cmdline) {
    p->cmdline = eos_psram_strdup(cmdline);
  } else {
    p->cmdline = NULL;
  }

  strlcpy(p->tty, "/dev/console", sizeof(p->tty));

  kv_init(p->fds);
  kv_init(p->dirs);
  kv_init(p->memblocks);
  kv_init(p->env);
  kv_init(p->threads);
  kv_init(p->fd_map);

  // Clone environment from parent
  if (cur && kv_size(cur->env) > 0) {
    for (size_t i = 0; i < kv_size(cur->env); i++) {
      char *entry = kv_A(cur->env, i);
      if (entry) {
        char *dup_e = eos_psram_strdup(entry);
        if (dup_e) {
          kv_push(char *, p->env, dup_e);
        }
      }
    }
    eos_proc_rebuild_env_array(p);
  } else {
    eos_proc_init_env(p);
  }

  proc_unlock();
  eos_proc_heap_tracking_restore(prev_h);
  return new_pid;
}

static void eos_proc_add_thread_locked(eos_proc_t *proc, TaskHandle_t thread) {
  if (!proc || !thread) return;
  bool prev_h = eos_proc_heap_tracking_disable();
  for (size_t i = 0; i < kv_size(proc->threads); i++) {
    if (kv_A(proc->threads, i) == thread) {
      eos_proc_heap_tracking_restore(prev_h);
      return;
    }
  }
  kv_push(TaskHandle_t, proc->threads, thread);
  eos_proc_heap_tracking_restore(prev_h);
}

void eos_proc_set_thread(pid_t pid, TaskHandle_t thread) {
  proc_lock();
  if (s_proc_table && thread) {
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (!s_proc_table[i].in_use) continue;
      if (s_proc_table[i].pid != pid) {
        if (s_proc_table[i].main_thread == thread) {
          s_proc_table[i].main_thread = NULL;
        }
        for (size_t j = 0; j < kv_size(s_proc_table[i].threads); j++) {
          if (kv_A(s_proc_table[i].threads, j) == thread) {
            kv_drop_fast(TaskHandle_t, s_proc_table[i].threads, j);
            break;
          }
        }
      }
    }
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
        s_proc_table[i].main_thread = thread;
        eos_proc_add_thread_locked(&s_proc_table[i], thread);
        break;
      }
    }
  }
  proc_unlock();
}

void eos_proc_set_state(pid_t pid, eos_proc_state_t state) {
  proc_lock();
  if (s_proc_table) {
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
        s_proc_table[i].state = state;
        break;
      }
    }
  }
  proc_unlock();
}

void eos_proc_set_tty(pid_t pid, const char *tty_name) {
  proc_lock();
  if (s_proc_table && tty_name) {
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
        strlcpy(s_proc_table[i].tty, tty_name, sizeof(s_proc_table[i].tty));
        break;
      }
    }
  }
  proc_unlock();
}

void eos_proc_set_cwd(pid_t pid, const char *cwd) {
  if (!cwd) return;
  proc_lock();
  if (s_proc_table) {
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
        strlcpy(s_proc_table[i].cwd, cwd, sizeof(s_proc_table[i].cwd));
        break;
      }
    }
  }
  proc_unlock();
}

void eos_proc_exit(pid_t pid, int exit_code) {
  if (pid == 0) {
    return; // Kernel context never exits
  }
  proc_lock();
  if (!s_proc_table) {
    proc_unlock();
    return;
  }

  eos_proc_t *p = NULL;
  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
      p = &s_proc_table[i];
      break;
    }
  }

  if (p) {
    if (p->state == EOS_PROC_STATE_ZOMBIE) {
      proc_unlock();
      return;
    }

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    for (size_t j = 0; j < kv_size(p->threads); j++) {
      TaskHandle_t th = kv_A(p->threads, j);
      if (th) {
        TaskStatus_t details;
        vTaskGetInfo(th, &details, pdFALSE, eInvalid);
        p->cpu_time_us += (uint64_t)details.ulRunTimeCounter;
      }
    }
#endif

    // Kill any background threads belonging to this process when main thread exits
    TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
    for (size_t j = 0; j < kv_size(p->threads); j++) {
      TaskHandle_t th = kv_A(p->threads, j);
      if (th && th != cur_task) {
        eos_proc_task_flags_cleanup(th);
        vTaskDelete(th);
      }
    }
    kv_destroy(p->threads);
    kv_init(p->threads);

    // Clean up process file descriptors and directories
    while (kv_size(p->fds) > 0) {
      int fd = kv_pop(p->fds);
      close(fd);
    }
    eos_proc_fd_map_reset(p);

    while (kv_size(p->dirs) > 0) {
      DIR *d = kv_pop(p->dirs);
      closedir(d);
    }

    p->exit_code = exit_code;
    p->last_exit_status = exit_code;
    p->state = EOS_PROC_STATE_ZOMBIE;
    p->main_thread = NULL;

    // Orphan re-parenting: all children of this process are re-parented to PID 1 (the init shell)
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use && s_proc_table[i].ppid == pid) {
        s_proc_table[i].ppid = 1;
      }
    }

    pthread_cond_broadcast(&p->wait_cond);
    pthread_cond_broadcast(&s_proc_table_cond);
  }

  proc_unlock();
}

eos_proc_t *eos_proc_find(pid_t pid) {
  if (pid == 0) return s_kernel_proc;
  if (!s_proc_table) return NULL;
  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
      return &s_proc_table[i];
    }
  }
  return NULL;
}

bool eos_proc_get_copy(pid_t pid, eos_proc_t *out_proc) {
  if (!out_proc) return false;
  if (pid == 0) {
    if (!s_kernel_proc) return false;
    pthread_mutex_lock(&s_kernel_proc->mem_mutex);
    memcpy(out_proc, s_kernel_proc, sizeof(eos_proc_t));
    pthread_mutex_unlock(&s_kernel_proc->mem_mutex);
    return true;
  }
  if (!s_proc_table) return false;
  proc_lock();
  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
      memcpy(out_proc, &s_proc_table[i], sizeof(eos_proc_t));
      proc_unlock();
      return true;
    }
  }
  proc_unlock();
  return false;
}

size_t eos_proc_get_all_pids(pid_t *out_pids, size_t max_pids) {
  if (!out_pids || max_pids == 0) return 0;
  proc_lock();
  size_t count = 0;
  out_pids[count++] = 0;
  if (s_proc_table) {
    for (size_t i = 0; i < EOS_MAX_PROCS && count < max_pids; i++) {
      if (s_proc_table[i].in_use) {
        out_pids[count++] = s_proc_table[i].pid;
      }
    }
  }
  proc_unlock();
  return count;
}

pid_t eos_proc_get_pgid(pid_t pid) {
  proc_lock();
  pid_t pgid = -1;
  if (s_proc_table) {
    if (pid == 0) {
      eos_proc_t *proc = eos_proc_current();
      pgid = (proc && proc->pgid > 0) ? proc->pgid : 1;
    } else {
      for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
        if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
          pgid = s_proc_table[i].pgid;
          break;
        }
      }
    }
  }
  proc_unlock();
  if (pgid < 0) {
    errno = ESRCH;
  }
  return pgid;
}

int eos_proc_set_pgid(pid_t pid, pid_t pgid) {
  proc_lock();
  int ret = -1;
  if (!s_proc_table) {
    proc_unlock();
    errno = ESRCH;
    return -1;
  }

  if (pid == 0) {
    eos_proc_t *proc = eos_proc_current();
    pid = (proc && proc->pid > 0) ? proc->pid : 1;
  }
  if (pgid == 0) {
    pgid = pid;
  }

  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
      s_proc_table[i].pgid = pgid;
      ret = 0;
      break;
    }
  }

  if (ret != 0) {
    errno = ESRCH;
  }

  proc_unlock();
  return ret;
}

pid_t eos_proc_setsid(pid_t pid) {
  proc_lock();
  pid_t sid = -1;
  if (!s_proc_table) {
    proc_unlock();
    errno = ESRCH;
    return -1;
  }

  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
      s_proc_table[i].sid = pid;
      s_proc_table[i].pgid = pid;
      sid = pid;
      break;
    }
  }

  if (sid < 0) {
    errno = ESRCH;
  }

  proc_unlock();
  return sid;
}

size_t eos_proc_count_active(void) {
  proc_lock();
  size_t count = 0;
  if (s_proc_table) {
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use) {
        count++;
      }
    }
  }
  proc_unlock();
  return count;
}

size_t eos_proc_snapshot(eos_proc_t *out_table, size_t max_count) {
  if (!out_table || max_count == 0) return 0;
  proc_lock();
  size_t written = 0;
  if (s_proc_table) {
    for (size_t i = 0; i < EOS_MAX_PROCS && written < max_count; i++) {
      if (s_proc_table[i].in_use) {
        memcpy(&out_table[written], &s_proc_table[i], sizeof(eos_proc_t));
        written++;
      }
    }
  }
  proc_unlock();
  return written;
}

pid_t eos_waitpid(pid_t pid, int *status, int options) {
  if (!s_proc_table) {
    eos_proc_init();
  }

  proc_lock();

  eos_proc_t *cur = eos_proc_current();
  pid_t calling_pid = (cur && cur->pid > 0) ? cur->pid : 1;
  pid_t calling_pgid = (cur && cur->pgid > 0) ? cur->pgid : 1;

  while (1) {
    bool has_matching_child = false;
    int zombie_slot = -1;

    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (!s_proc_table[i].in_use) continue;
      eos_proc_t *p = &s_proc_table[i];

      bool match = false;
      if (pid == -1) {
        match = (p->ppid == calling_pid);
      } else if (pid > 0) {
        match = (p->pid == pid) && (p->ppid == calling_pid || calling_pid == 1);
      } else if (pid == 0) {
        match = (p->pgid == calling_pgid && (p->ppid == calling_pid || calling_pid == 1));
      } else {
        match = (p->pgid == -pid && (p->ppid == calling_pid || calling_pid == 1));
      }

      if (match) {
        has_matching_child = true;
        if (p->state == EOS_PROC_STATE_ZOMBIE) {
          zombie_slot = (int)i;
          break;
        }
      }
    }

    if (zombie_slot >= 0) {
      eos_proc_t *z = &s_proc_table[zombie_slot];
      pid_t reaped_pid = z->pid;
      int raw_status = (z->exit_code & 0xff) << 8;

      if (status) {
        *status = raw_status;
      }

      // Reap the zombie & clean up all process resources
      bool prev_h = eos_proc_heap_tracking_disable();
      extern void __real_heap_caps_free(void *ptr);
      extern int __real__close_r(struct _reent *r, int fd);
      extern int __real_closedir(DIR *pdir);

      for (size_t j = 0; j < kv_size(z->fds); j++) {
        int fd = kv_A(z->fds, j);
        if (fd >= 0) {
          eos_vfs_fd_lock(fd);
          __real__close_r(__getreent(), fd);
          eos_vfs_fd_unregister(fd);
          eos_vfs_fd_unlock(fd);
        }
      }

      for (size_t j = 0; j < kv_size(z->dirs); j++) {
        DIR *d = kv_A(z->dirs, j);
        if (d) {
          proc_memblock_remove_locked(z, (void *)d, NULL, NULL);
          __real_closedir(d);
          eos_vfs_dir_close(d);
        }
      }

      if (z->cmdline) {
        proc_memblock_remove_locked(z, z->cmdline, NULL, NULL);
        eos_psram_free(z->cmdline);
        z->cmdline = NULL;
      }

      if (z->env_array) {
        proc_memblock_remove_locked(z, z->env_array, NULL, NULL);
        __real_heap_caps_free(z->env_array);
        z->env_array = NULL;
      }

      for (size_t j = 0; j < kv_size(z->env); j++) {
        char *e = kv_A(z->env, j);
        if (e) {
          proc_memblock_remove_locked(z, e, NULL, NULL);
          __real_heap_caps_free(e);
        }
      }

      // Ensure any kvec internal buffers are removed from memblocks tracking before free
      proc_memblock_remove_locked(z, z->fds.a, NULL, NULL);
      proc_memblock_remove_locked(z, z->dirs.a, NULL, NULL);
      proc_memblock_remove_locked(z, z->env.a, NULL, NULL);
      proc_memblock_remove_locked(z, z->threads.a, NULL, NULL);
      proc_memblock_remove_locked(z, z->fd_map.a, NULL, NULL);
      proc_memblock_remove_locked(z, z->memblocks.a, NULL, NULL);

      for (size_t j = 0; j < kv_size(z->memblocks); j++) {
        void *b = kv_A(z->memblocks, j).ptr;
        if (b && !kv_owns_buffer(z->memblocks, b) &&
            !kv_owns_buffer(z->fds, b) &&
            !kv_owns_buffer(z->dirs, b) &&
            !kv_owns_buffer(z->env, b) &&
            !kv_owns_buffer(z->threads, b) &&
            !kv_owns_buffer(z->fd_map, b)) {
          __real_heap_caps_free(b);
        }
      }

      kv_destroy(z->fds);
      kv_destroy(z->dirs);
      kv_destroy(z->env);
      kv_destroy(z->threads);
      kv_destroy(z->fd_map);
      kv_destroy(z->memblocks);

      pthread_mutex_t saved_mem_mutex = z->mem_mutex;
      pthread_cond_t saved_wait_cond = z->wait_cond;
      memset(z, 0, sizeof(eos_proc_t));
      z->mem_mutex = saved_mem_mutex;
      z->wait_cond = saved_wait_cond;
      z->in_use = false;
      z->state = EOS_PROC_STATE_UNUSED;

      eos_proc_heap_tracking_restore(prev_h);

      proc_unlock();
      vTaskDelay(1);
      return reaped_pid;
    }

    if (!has_matching_child) {
      proc_unlock();
      errno = ECHILD;
      return -1;
    }

    if (options & WNOHANG) {
      proc_unlock();
      return 0;
    }

    // Blocking wait for child process termination
    pthread_cond_wait(&s_proc_table_cond, &s_proc_mutex);
  }
}

pid_t eos_getpid(void) {
  eos_proc_t *proc = eos_proc_current();
  return (proc && proc->pid > 0) ? proc->pid : 1;
}

pid_t eos_getppid(void) {
  eos_proc_t *proc = eos_proc_current();
  return (proc && proc->ppid > 0) ? proc->ppid : 0;
}

pid_t eos_getpgid(pid_t pid) {
  return eos_proc_get_pgid(pid);
}

int eos_setpgid(pid_t pid, pid_t pgid) {
  return eos_proc_set_pgid(pid, pgid);
}

pid_t eos_setsid(void) {
  return eos_proc_setsid(eos_getpid());
}

pid_t getppid(void) {
  return eos_getppid();
}

pid_t getpgid(pid_t pid) {
  return eos_getpgid(pid);
}

int setpgid(pid_t pid, pid_t pgid) {
  return eos_setpgid(pid, pgid);
}

pid_t setsid(void) {
  return eos_setsid();
}

pid_t waitpid(pid_t pid, int *status, int options) {
  return eos_waitpid(pid, status, options);
}

// ── Memory Block Allocation Tracking & Registry (F02) ──────────

static int memblock_bsearch(const eos_memblock_t *arr, size_t n, const void *ptr, bool *found) {
  int low = 0;
  int high = (int)n - 1;
  while (low <= high) {
    int mid = low + (high - low) / 2;
    if (arr[mid].ptr == ptr) {
      if (found) *found = true;
      return mid;
    }
    if ((uintptr_t)arr[mid].ptr < (uintptr_t)ptr) {
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  if (found) *found = false;
  return low;
}

void eos_proc_mem_insert(eos_proc_t *proc, void *ptr, size_t size, bool is_psram) {
  if (!proc || !ptr) return;

  bool prev_h = eos_proc_heap_tracking_disable();
  pthread_mutex_lock(&proc->mem_mutex);
  size_t n = kv_size(proc->memblocks);
  bool found = false;
  int idx = memblock_bsearch(kv_data(proc->memblocks), n, ptr, &found);

  if (found) {
    if (kv_A(proc->memblocks, idx).is_psram) {
      proc->heap_psram = (proc->heap_psram >= kv_A(proc->memblocks, idx).size) ?
                         (proc->heap_psram - kv_A(proc->memblocks, idx).size) : 0;
    } else {
      proc->heap_sram = (proc->heap_sram >= kv_A(proc->memblocks, idx).size) ?
                        (proc->heap_sram - kv_A(proc->memblocks, idx).size) : 0;
    }
    kv_A(proc->memblocks, idx).size = size;
    kv_A(proc->memblocks, idx).is_psram = is_psram;
  } else {
    eos_memblock_t mb = { .ptr = ptr, .size = size, .is_psram = is_psram };
    kv_insert(eos_memblock_t, proc->memblocks, idx, mb);
  }

  if (is_psram) {
    proc->heap_psram += size;
  } else {
    proc->heap_sram += size;
  }
  pthread_mutex_unlock(&proc->mem_mutex);
  eos_proc_heap_tracking_restore(prev_h);
}

static bool proc_memblock_remove_locked(eos_proc_t *proc, void *ptr, size_t *out_size, bool *out_is_psram) {
  size_t n = kv_size(proc->memblocks);
  if (n == 0) return false;

  bool found = false;
  int idx = memblock_bsearch(kv_data(proc->memblocks), n, ptr, &found);
  if (!found) return false;

  eos_memblock_t mb = kv_A(proc->memblocks, idx);
  if (mb.is_psram) {
    proc->heap_psram = (proc->heap_psram >= mb.size) ? (proc->heap_psram - mb.size) : 0;
  } else {
    proc->heap_sram = (proc->heap_sram >= mb.size) ? (proc->heap_sram - mb.size) : 0;
  }

  if (out_size) *out_size = mb.size;
  if (out_is_psram) *out_is_psram = mb.is_psram;

  kv_drop(eos_memblock_t, proc->memblocks, idx);
  return true;
}

void eos_proc_mem_remove(eos_proc_t *proc, void *ptr) {
  if (!ptr) return;
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) return;
  const char *tname = pcTaskGetName(NULL);
  if (tname && (strncmp(tname, "IDLE", 4) == 0 || strncmp(tname, "ipc", 3) == 0)) return;

  if (proc && proc->in_use) {
    pthread_mutex_lock(&proc->mem_mutex);
    bool removed = proc_memblock_remove_locked(proc, ptr, NULL, NULL);
    pthread_mutex_unlock(&proc->mem_mutex);
    if (removed) return;
  }

  // Fallback: check PID 0 (kernel context)
  if (s_kernel_proc && proc != s_kernel_proc) {
    pthread_mutex_lock(&s_kernel_proc->mem_mutex);
    bool removed = proc_memblock_remove_locked(s_kernel_proc, ptr, NULL, NULL);
    pthread_mutex_unlock(&s_kernel_proc->mem_mutex);
    if (removed) return;
  }

  // Fallback: search other active processes (cross-process frees, e.g. parent allocated, child freed)
  if (s_proc_table) {
    proc_lock();
    for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
      if (s_proc_table[i].in_use && &s_proc_table[i] != proc && (!s_kernel_proc || &s_proc_table[i] != s_kernel_proc)) {
        pthread_mutex_lock(&s_proc_table[i].mem_mutex);
        bool found = proc_memblock_remove_locked(&s_proc_table[i], ptr, NULL, NULL);
        pthread_mutex_unlock(&s_proc_table[i].mem_mutex);
        if (found) {
          proc_unlock();
          return;
        }
      }
    }
    proc_unlock();
  }
}

void eos_proc_mem_realloc(eos_proc_t *proc, void *old_ptr, void *new_ptr, size_t new_size, bool is_psram) {
  if (!proc) return;
  if (old_ptr == new_ptr && old_ptr != NULL) {
    eos_proc_mem_insert(proc, new_ptr, new_size, is_psram);
    return;
  }
  if (old_ptr) {
    eos_proc_mem_remove(proc, old_ptr);
  }
  if (new_ptr) {
    eos_proc_mem_insert(proc, new_ptr, new_size, is_psram);
  }
}

int eos_mem_adopt(void *ptr, pid_t target_pid) {
  if (!ptr) {
    errno = EINVAL;
    return -1;
  }

  eos_proc_t *target = eos_proc_find(target_pid);
  if (!target || !target->in_use) {
    errno = ESRCH;
    return -1;
  }

  eos_proc_t *current = eos_proc_current();
  size_t size = 0;
  bool is_psram = false;
  bool found = false;

  if (current) {
    pthread_mutex_lock(&current->mem_mutex);
    found = proc_memblock_remove_locked(current, ptr, &size, &is_psram);
    pthread_mutex_unlock(&current->mem_mutex);
  }

  if (!found && s_kernel_proc && current != s_kernel_proc) {
    pthread_mutex_lock(&s_kernel_proc->mem_mutex);
    found = proc_memblock_remove_locked(s_kernel_proc, ptr, &size, &is_psram);
    pthread_mutex_unlock(&s_kernel_proc->mem_mutex);
  }

  if (!found && s_proc_table) {
    proc_lock();
    for (size_t i = 0; i < EOS_MAX_PROCS && !found; i++) {
      if (s_proc_table[i].in_use && s_proc_table[i].state != EOS_PROC_STATE_ZOMBIE && &s_proc_table[i] != current) {
        pthread_mutex_lock(&s_proc_table[i].mem_mutex);
        found = proc_memblock_remove_locked(&s_proc_table[i], ptr, &size, &is_psram);
        pthread_mutex_unlock(&s_proc_table[i].mem_mutex);
      }
    }
    proc_unlock();
  }

  if (!found) {
    errno = ENOENT;
    return -1;
  }

  eos_proc_mem_insert(target, ptr, size, is_psram);
  return 0;
}

void eos_proc_reg_memblock(void *block) {
  if (!block) return;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    eos_proc_mem_insert(proc, block, 0, false);
  }
}

void eos_proc_unreg_memblock(void *block) {
  if (!block) return;
  eos_proc_t *proc = eos_proc_current();
  eos_proc_mem_remove(proc, block);
}

// ── File Descriptor & Directory Tracking ──────────────────────

int eos_proc_resolve_fd(int fd) {
  if (fd < 0 || fd >= FD_SETSIZE) return fd;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return fd;
  int mapped = eos_proc_fd_map_get(proc, fd);
  return (mapped >= 0) ? mapped : fd;
}

void eos_proc_reg_fd(int fd) {
  if (fd < 0) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;
  bool prev_h = eos_proc_heap_tracking_disable();
  kv_push(int, proc->fds, fd);
  eos_proc_heap_tracking_restore(prev_h);
}

void eos_proc_unreg_fd(int fd) {
  if (fd < 0) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;

  bool prev_h = eos_proc_heap_tracking_disable();
  for (size_t i = 0; i < kv_size(proc->fds); i++) {
    if (kv_A(proc->fds, i) == fd) {
      kv_drop_fast(int, proc->fds, i);
      break;
    }
  }
  eos_proc_heap_tracking_restore(prev_h);
}

void eos_proc_reg_dir(DIR *dir) {
  if (!dir) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;
  bool prev_h = eos_proc_heap_tracking_disable();
  kv_push(DIR *, proc->dirs, dir);
  eos_proc_heap_tracking_restore(prev_h);
}

void eos_proc_unreg_dir(DIR *dir) {
  if (!dir) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;

  bool prev_h = eos_proc_heap_tracking_disable();
  for (size_t i = 0; i < kv_size(proc->dirs); i++) {
    if (kv_A(proc->dirs, i) == dir) {
      kv_drop_fast(DIR *, proc->dirs, i);
      break;
    }
  }
  eos_proc_heap_tracking_restore(prev_h);
}

eos_proc_fd_snapshot_t eos_proc_snapshot_fds(void) {
  eos_proc_fd_snapshot_t snap = {0};
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return snap;

  snap.valid = true;
  size_t n_fds = kv_size(proc->fds);
  if (n_fds > 0) {
    extern void *__real_heap_caps_aligned_alloc_base(size_t, size_t, uint32_t);
    snap.fds = __real_heap_caps_aligned_alloc_base(4, n_fds * sizeof(int), MALLOC_CAP_DEFAULT);
    if (snap.fds) {
      for (size_t j = 0; j < n_fds; j++) {
        snap.fds[j] = kv_A(proc->fds, j);
      }
      snap.count = n_fds;
    }
  }

  size_t n_dirs = kv_size(proc->dirs);
  if (n_dirs > 0) {
    extern void *__real_heap_caps_aligned_alloc_base(size_t, size_t, uint32_t);
    snap.dirs = __real_heap_caps_aligned_alloc_base(4, n_dirs * sizeof(DIR *), MALLOC_CAP_DEFAULT);
    if (snap.dirs) {
      for (size_t j = 0; j < n_dirs; j++) {
        snap.dirs[j] = kv_A(proc->dirs, j);
      }
      snap.dir_count = n_dirs;
    }
  }

  return snap;
}

void eos_proc_cleanup_leaked_fds(eos_proc_fd_snapshot_t *snap) {
  if (!snap || !snap->valid) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    extern void __real_heap_caps_free(void *);
    if (snap->fds) __real_heap_caps_free(snap->fds);
    if (snap->dirs) __real_heap_caps_free(snap->dirs);
    snap->fds = NULL;
    snap->dirs = NULL;
    snap->count = 0;
    snap->dir_count = 0;
    snap->valid = false;
    return;
  }

  // 1. Close any leaked file descriptors not in the snapshot
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < kv_size(proc->fds); i++) {
      int fd = kv_A(proc->fds, i);
      bool was_present = false;
      if (snap->fds) {
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
        eos_proc_unreg_fd(fd);
        changed = true;
        break; // restart search since close() mutates proc->fds
      }
    }
  }

  // 2. Close any leaked directory handles not in the snapshot
  changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < kv_size(proc->dirs); i++) {
      DIR *dir = kv_A(proc->dirs, i);
      bool was_present = false;
      if (snap->dirs) {
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
        eos_proc_unreg_dir(dir);
        changed = true;
        break; // restart search since closedir() mutates proc->dirs
      }
    }
  }

  extern void __real_heap_caps_free(void *);
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
  snap->valid = false;
}

// ── Thread Management & Wrapping ──────────────────────────────

void eos_proc_add_thread(eos_proc_t *proc, TaskHandle_t thread) {
  if (!proc || !thread) return;
  proc_lock();
  eos_proc_add_thread_locked(proc, thread);
  proc_unlock();
}

void eos_proc_remove_thread(eos_proc_t *proc, TaskHandle_t thread) {
  if (!proc || !thread) return;
  bool prev_h = eos_proc_heap_tracking_disable();
  proc_lock();
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  TaskStatus_t details;
  vTaskGetInfo(thread, &details, pdFALSE, eInvalid);
  proc->cpu_time_us += (uint64_t)details.ulRunTimeCounter;
#endif
  if (proc->main_thread == thread) {
    proc->main_thread = NULL;
  }
  for (size_t i = 0; i < kv_size(proc->threads); i++) {
    if (kv_A(proc->threads, i) == thread) {
      kv_drop_fast(TaskHandle_t, proc->threads, i);
      kv_opt(TaskHandle_t, proc->threads);
      break;
    }
  }
  proc_unlock();
  eos_proc_heap_tracking_restore(prev_h);
}

uint64_t eos_proc_get_cpu_time_us(eos_proc_t *proc) {
  if (!proc) return 0;
  proc_lock();
  uint64_t total = proc->cpu_time_us;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  if (proc->state != EOS_PROC_STATE_ZOMBIE) {
    for (size_t i = 0; i < kv_size(proc->threads); i++) {
      TaskHandle_t th = kv_A(proc->threads, i);
      if (th) {
        TaskStatus_t details;
        vTaskGetInfo(th, &details, pdFALSE, eInvalid);
        total += (uint64_t)details.ulRunTimeCounter;
      }
    }
  }
#endif
  proc_unlock();
  return total;
}

eos_pwrap_t *eos_proc_wrap_prepare(void *thread_start, void *thread_data) {
  eos_proc_t *cur = eos_proc_current();
  if (!cur) {
    if (!s_proc_table) eos_proc_init();
    cur = &s_proc_table[0];
  }

  eos_pwrap_t *pwrap = eos_psram_calloc_prefer(1, sizeof(eos_pwrap_t));
  if (!pwrap) {
    pwrap = calloc(1, sizeof(eos_pwrap_t));
  }
  if (!pwrap) return NULL;

  pwrap->thread_start = thread_start;
  pwrap->thread_arg = thread_data;
  pwrap->proc = cur;
  pwrap->is_main = false;

  return pwrap;
}

int eos_proc_spawn_thread(pid_t pid, void *(*start_routine)(void *), void *arg,
                          uint32_t stack_size, bool prefer_psram, pthread_t *thread_out) {
  eos_proc_t *proc = eos_proc_find(pid);
  if (!proc || !proc->in_use) {
    return -ESRCH;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_pwrap_t *pwrap = eos_psram_calloc_prefer(1, sizeof(eos_pwrap_t));
  if (!pwrap) {
    pwrap = calloc(1, sizeof(eos_pwrap_t));
  }
  if (!pwrap) {
    eos_proc_heap_tracking_restore(prev_h);
    return -ENOMEM;
  }

  pwrap->thread_start = start_routine;
  pwrap->thread_arg = arg;
  pwrap->proc = proc;
  pwrap->is_main = true;

  if (stack_size < EOS_BIN_MIN_STACK_SIZE) {
    stack_size = EOS_BIN_DEFAULT_STACK_SIZE;
  }

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, stack_size);

  extern void eos_pthread_set_stack_cfg(uint32_t stack_size, bool prefer_psram, const char *name);
  eos_pthread_set_stack_cfg(stack_size, prefer_psram, proc->name);

  pthread_t dummy;
  pthread_t *t = thread_out ? thread_out : &dummy;

  extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                   void *(*start_routine)(void *), void *arg);
  int rc = __real_pthread_create(t, &attr, eos_proc_wrap_pthread, pwrap);
  if (rc != 0 && prefer_psram) {
    eos_pthread_set_stack_cfg(stack_size, false, proc->name);
    rc = __real_pthread_create(t, &attr, eos_proc_wrap_pthread, pwrap);
  }
  pthread_attr_destroy(&attr);

  esp_pthread_cfg_t default_cfg = esp_pthread_get_default_config();
  esp_pthread_set_cfg(&default_cfg);

  if (rc != 0) {
    free(pwrap);
  }
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

void *eos_proc_wrap_pthread(void *data) {
  eos_pwrap_t *pwrap = (eos_pwrap_t *)data;
  if (!pwrap) return NULL;

  eos_proc_t *proc = pwrap->proc;
  void *(*thread_start)(void *) = pwrap->thread_start;
  void *thread_arg = pwrap->thread_arg;
  bool is_main = pwrap->is_main;

  free(pwrap);

  TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();

  if (proc) {
    if (is_main) {
      proc_lock();
      proc->main_thread = cur_task;
      proc_unlock();
    }
    eos_proc_add_thread(proc, cur_task);
  }

  void *result = thread_start(thread_arg);

  fflush(stdout);
  fflush(stderr);
  extern void esp_reent_cleanup(void);
  esp_reent_cleanup();

  if (proc) {
    eos_proc_remove_thread(proc, cur_task);
    if (is_main) {
      proc_lock();
      if (proc->main_thread == cur_task) {
        proc->main_thread = NULL;
      }
      proc_unlock();
      eos_proc_exit(proc->pid, (int)(intptr_t)result);
    }
    eos_proc_task_flags_cleanup(cur_task);
  }

  return result;
}

void eos_proc_wrap_freertos(void *data) {
  eos_pwrap_t *pwrap = (eos_pwrap_t *)data;
  if (!pwrap) {
    vTaskDelete(NULL);
    return;
  }

  eos_proc_t *proc = pwrap->proc;
  void (*task_func)(void *) = (void (*)(void *))(uintptr_t)pwrap->thread_start;
  void *task_arg = pwrap->thread_arg;
  bool is_main = pwrap->is_main;

  free(pwrap);

  TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();

  if (proc) {
    eos_proc_add_thread(proc, cur_task);
  }

  if (task_func) {
    task_func(task_arg);
  }

  if (proc) {
    if (is_main) {
      eos_proc_exit(proc->pid, 0);
    }
    eos_proc_task_flags_cleanup(cur_task);
    eos_proc_remove_thread(proc, cur_task);
  }

  vTaskDelete(NULL);
}

size_t eos_thread_stack_high_water_mark(void) {
  return (size_t)uxTaskGetStackHighWaterMark(NULL);
}
