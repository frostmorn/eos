#include "ecore/proc.h"
#include "ecore/bin.h"
#include "ecore/dev.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/vfs.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <esp_event.h>
#include <esp_memory_utils.h>
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

typedef struct {
  kvec_psram_t(eos_proc_t *) procs;
  pthread_mutex_t table_lock;
  pthread_cond_t cond;
  pid_t next_pid;
  eos_proc_t *kernel_proc;
} eos_proc_table_t;

static eos_proc_table_t *s_proc_table = NULL;

static inline void proc_lock(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || !s_proc_table) {
    return;
  }
  pthread_mutex_lock(&s_proc_table->table_lock);
}

static inline void proc_unlock(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || !s_proc_table) {
    return;
  }
  pthread_mutex_unlock(&s_proc_table->table_lock);
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

static char *eos_proc_getenv_internal(eos_proc_t *proc, const char *name) {
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

char *eos_proc_getenv(const char *name) {
  eos_proc_t *proc = eos_proc_current();
  return eos_proc_getenv_internal(proc, name);
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

int eos_proc_setenv_pid(pid_t pid, const char *name, const char *value, int overwrite) {
  proc_lock();
  if (!s_proc_table) {
    proc_unlock();
    return -1;
  }
  eos_proc_t *p = NULL;
  for (size_t i = 0; i < kv_size(s_proc_table->procs); i++) {
    eos_proc_t *slot = kv_A(s_proc_table->procs, i);
    if (slot && slot->in_use && slot->pid == pid) {
      p = slot;
      break;
    }
  }
  if (!p) {
    proc_unlock();
    return -1;
  }
  int rc = eos_proc_setenv_internal(p, name, value, overwrite);
  proc_unlock();
  return rc;
}

char *eos_proc_getenv_pid(pid_t pid, const char *name) {
  proc_lock();
  if (!s_proc_table) {
    proc_unlock();
    return NULL;
  }
  eos_proc_t *p = NULL;
  for (size_t i = 0; i < kv_size(s_proc_table->procs); i++) {
    eos_proc_t *slot = kv_A(s_proc_table->procs, i);
    if (slot && slot->in_use && slot->pid == pid) {
      p = slot;
      break;
    }
  }
  if (!p) {
    proc_unlock();
    return NULL;
  }
  char *val = eos_proc_getenv_internal(p, name);
  proc_unlock();
  return val;
}

int eos_proc_unsetenv_pid(pid_t pid, const char *name) {
  proc_lock();
  if (!s_proc_table || !name) {
    proc_unlock();
    return -1;
  }
  eos_proc_t *proc = NULL;
  for (size_t i = 0; i < kv_size(s_proc_table->procs); i++) {
    eos_proc_t *slot = kv_A(s_proc_table->procs, i);
    if (slot && slot->in_use && slot->pid == pid) {
      proc = slot;
      break;
    }
  }
  if (!proc) {
    proc_unlock();
    return -1;
  }
  extern void __real_heap_caps_free(void *ptr);
  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(proc->env); i++) {
    char *entry = kv_A(proc->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      __real_heap_caps_free(entry);
      kv_drop_fast(char *, proc->env, i);
      kv_opt(char *, proc->env);
      eos_proc_rebuild_env_array(proc);
      proc_unlock();
      eos_log_sync_env(name, NULL);
      return 0;
    }
  }
  proc_unlock();
  eos_log_sync_env(name, NULL);
  return 0;
}

int eos_proc_unsetenv(const char *name) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc || !name) return -1;
  return eos_proc_unsetenv_pid(proc->pid, name);
}

void eos_proc_resolve_display_env(pid_t pid, uint32_t bin_flags) {
  const char *force_fb = eos_proc_getenv_pid(pid, "EOS_FB_FORCE");
  if (force_fb && force_fb[0] != '\0') {
    eos_proc_setenv_pid(pid, "EOS_FB", force_fb, 1);
  } else {
    uint32_t role_val = EOS_DISPLAY_ROLE_MAIN;
    if (bin_flags & EOS_BIN_FLAG_ROLE_MASK) {
      role_val = (bin_flags & EOS_BIN_FLAG_ROLE_MASK) >> EOS_BIN_FLAG_ROLE_SHIFT;
    }

    const char *role_env = NULL;
    const char *def_fb = "/dev/vfb0";
    switch (role_val) {
    case EOS_DISPLAY_ROLE_FULL:
      role_env = "EOS_FB_FULL";
      def_fb = "/dev/vfb3";
      break;
    case EOS_DISPLAY_ROLE_STATUS:
      role_env = "EOS_FB_STATUS";
      def_fb = "/dev/vfb1";
      break;
    case EOS_DISPLAY_ROLE_TOOLBAR:
      role_env = "EOS_FB_TOOLBAR";
      def_fb = "/dev/vfb2";
      break;
    case EOS_DISPLAY_ROLE_MAIN:
    default:
      role_env = "EOS_FB_MAIN";
      def_fb = "/dev/vfb0";
      break;
    }

    const char *assigned_fb = role_env ? eos_proc_getenv_pid(pid, role_env) : NULL;
    if (assigned_fb && assigned_fb[0] != '\0') {
      eos_proc_setenv_pid(pid, "EOS_FB", assigned_fb, 1);
    } else {
      const char *dev_name = (def_fb[0] == '/' && strncmp(def_fb, "/dev/", 5) == 0) ? def_fb + 5 : def_fb;
      if (!eos_dev_find_by_name(dev_name)) {
        def_fb = "/dev/fb0";
      }
      eos_proc_setenv_pid(pid, "EOS_FB", def_fb, 1);
    }
  }

  const char *force_touch = eos_proc_getenv_pid(pid, "EOS_TOUCH_FORCE");
  if (force_touch && force_touch[0] != '\0') {
    eos_proc_setenv_pid(pid, "EOS_TOUCH", force_touch, 1);
  }
}

char **eos_proc_get_environ(void) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return NULL;
  return proc->env_array;
}

static pid_t alloc_next_pid_locked(void) {
  if (!s_proc_table) return -1;
  for (int attempt = 0; attempt < 32767; attempt++) {
    pid_t cand = s_proc_table->next_pid++;
    if (s_proc_table->next_pid > 32767) {
      s_proc_table->next_pid = 2;
    }
    if (cand < 2) {
      cand = 2;
    }

    bool collision = false;
    size_t count = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < count; i++) {
      eos_proc_t *p = kv_A(s_proc_table->procs, i);
      if (p && p->in_use && p->pid == cand) {
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

enum {
  EOS_TASK_STATE_USER = 0,
  EOS_TASK_STATE_SYS  = 1,
  EOS_TASK_STATE_WAIT = 2,
};

typedef struct {
  TaskHandle_t task;
  eos_proc_t *proc;
  uint64_t last_state_time;
  uint32_t state : 4;
  uint32_t in_heap_wrap : 1;
  uint32_t is_system : 1;
  uint32_t is_lwip : 1;
  uint32_t is_classified : 1;
  uint32_t kernel_depth : 10;
  uint32_t wait_depth : 10;
  uint32_t heap_dis_depth : 10;
} eos_task_flags_t;

static eos_task_flags_t *s_task_flags = NULL;
static portMUX_TYPE s_task_flags_spinlock = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_task_flags_active_count = 0;

static inline void task_flags_lock(void) {
  portENTER_CRITICAL(&s_task_flags_spinlock);
}

static inline void task_flags_unlock(void) {
  portEXIT_CRITICAL(&s_task_flags_spinlock);
}

static void eos_proc_task_set_proc(TaskHandle_t task, eos_proc_t *proc);

static void classify_task_locked(eos_task_flags_t *entry, TaskHandle_t task, const char *hint_name) {
  if (entry->is_classified) return;
  const char *name = hint_name ? hint_name : pcTaskGetName(task);
  if (name) {
    if (strcmp(name, "tiT") == 0 || strcmp(name, "tcpip") == 0) {
      entry->is_lwip = 1;
      entry->is_system = 1;
    } else if (strncmp(name, "IDLE", 4) == 0 ||
               strncmp(name, "ipc", 3) == 0 ||
               strncmp(name, "esp_", 4) == 0 ||
               strncmp(name, "sdio_", 5) == 0 ||
               strncmp(name, "rpc_", 4) == 0 ||
               strcmp(name, "Tmr Svc") == 0 ||
               strcmp(name, "sys_evt") == 0 ||
               strcmp(name, "wifi") == 0) {
      entry->is_system = 1;
    }
  }
  entry->is_classified = 1;
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
    memset(&s_task_flags[free_idx], 0, sizeof(eos_task_flags_t));
    s_task_flags[free_idx].task = task;
    classify_task_locked(&s_task_flags[free_idx], task, NULL);
    s_task_flags_active_count++;
    return &s_task_flags[free_idx];
  }
  return NULL;
}

static void release_task_flags_locked(eos_task_flags_t *entry) {
  if (entry && entry->task && !entry->is_system && !entry->is_lwip) {
    if (entry->kernel_depth == 0 &&
        entry->heap_dis_depth == 0 &&
        !entry->in_heap_wrap &&
        entry->proc == NULL) {
      memset(entry, 0, sizeof(eos_task_flags_t));
      if (s_task_flags_active_count > 0) {
        s_task_flags_active_count--;
      }
    }
  }
}

static void eos_proc_task_set_proc(TaskHandle_t task, eos_proc_t *proc) {
  if (!task) return;
  task_flags_lock();
  if (s_task_flags) {
    eos_task_flags_t *entry = get_task_flags_locked(task, true);
    if (entry) {
      entry->proc = proc;
      if (entry->last_state_time == 0) {
        entry->last_state_time = (uint64_t)esp_timer_get_time();
        entry->state = EOS_TASK_STATE_USER;
      }
    }
  }
  task_flags_unlock();
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
  bool ret = (entry != NULL && entry->is_lwip != 0);
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
  bool ret = (entry != NULL && entry->is_system != 0);
  task_flags_unlock();
  return ret;
}

void eos_proc_task_flags_cleanup(void *task_ptr) {
  TaskHandle_t task = (TaskHandle_t)task_ptr;
  if (!task || !s_task_flags || s_task_flags_active_count == 0) return;
  eos_proc_t *proc = NULL;
  uint64_t delta = 0;
  int state = -1;

  task_flags_lock();
  if (s_task_flags) {
    for (int i = 0; i < EOS_MAX_TASK_FLAGS; i++) {
      if (s_task_flags[i].task == task) {
        if (s_task_flags[i].proc && s_task_flags[i].last_state_time > 0) {
          uint64_t now = (uint64_t)esp_timer_get_time();
          delta = (now >= s_task_flags[i].last_state_time) ? (now - s_task_flags[i].last_state_time) : 0;
          proc = s_task_flags[i].proc;
          state = (int)s_task_flags[i].state;
        }
        memset(&s_task_flags[i], 0, sizeof(eos_task_flags_t));
        if (s_task_flags_active_count > 0) {
          s_task_flags_active_count--;
        }
        break;
      }
    }
  }
  task_flags_unlock();

  if (proc && delta > 0) {
    proc_lock();
    if (state == EOS_TASK_STATE_USER) {
      proc->user_us += delta;
    } else if (state == EOS_TASK_STATE_SYS) {
      proc->sys_us += delta;
    } else if (state == EOS_TASK_STATE_WAIT) {
      proc->wait_us += delta;
    }
    proc->cpu_time_us = proc->user_us + proc->sys_us;
    proc_unlock();
  }
}

bool eos_proc_enter_kernel(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  eos_proc_t *proc = NULL;
  uint64_t user_delta = 0;
  bool prev = true;

  task_flags_lock();
  if (s_task_flags) {
    eos_task_flags_t *entry = get_task_flags_locked(task, true);
    if (entry) {
      uint32_t depth = entry->kernel_depth;
      prev = (depth > 0);
      uint64_t now = (uint64_t)esp_timer_get_time();
      if (depth == 0) {
        if (entry->state == EOS_TASK_STATE_USER && entry->last_state_time > 0) {
          user_delta = (now >= entry->last_state_time) ? (now - entry->last_state_time) : 0;
          proc = entry->proc;
        }
        entry->state = EOS_TASK_STATE_SYS;
        entry->last_state_time = now;
      }
      entry->kernel_depth = depth + 1;
    }
  }
  task_flags_unlock();

  if (proc && user_delta > 0) {
    proc_lock();
    proc->user_us += user_delta;
    proc->cpu_time_us = proc->user_us + proc->sys_us;
    proc_unlock();
  }

  return prev;
}

void eos_proc_leave_kernel(bool prev) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return;

  eos_proc_t *proc = NULL;
  uint64_t sys_delta = 0;

  task_flags_lock();
  if (s_task_flags) {
    eos_task_flags_t *entry = get_task_flags_locked(task, false);
    if (entry) {
      if (entry->kernel_depth > 0) {
        entry->kernel_depth--;
      }
      if (!prev) {
        entry->kernel_depth = 0;
      }
      if (entry->kernel_depth == 0) {
        uint64_t now = (uint64_t)esp_timer_get_time();
        if (entry->state == EOS_TASK_STATE_SYS && entry->wait_depth == 0 && entry->last_state_time > 0) {
          sys_delta = (now >= entry->last_state_time) ? (now - entry->last_state_time) : 0;
          proc = entry->proc;
        }
        entry->state = EOS_TASK_STATE_USER;
        entry->last_state_time = now;
      }
      release_task_flags_locked(entry);
    }
  }
  task_flags_unlock();

  if (proc && sys_delta > 0) {
    proc_lock();
    proc->sys_us += sys_delta;
    proc->cpu_time_us = proc->user_us + proc->sys_us;
    proc_unlock();
  }
}

bool eos_proc_enter_wait(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return true;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return true;

  eos_proc_t *proc = NULL;
  uint64_t delta = 0;
  int from_state = -1;
  bool prev = true;

  task_flags_lock();
  if (s_task_flags) {
    eos_task_flags_t *entry = get_task_flags_locked(task, true);
    if (entry) {
      uint32_t depth = entry->wait_depth;
      prev = (depth > 0);
      uint64_t now = (uint64_t)esp_timer_get_time();
      if (depth == 0) {
        if (entry->last_state_time > 0) {
          delta = (now >= entry->last_state_time) ? (now - entry->last_state_time) : 0;
          proc = entry->proc;
          from_state = (int)entry->state;
        }
        entry->state = EOS_TASK_STATE_WAIT;
        entry->last_state_time = now;
      }
      entry->wait_depth = depth + 1;
    }
  }
  task_flags_unlock();

  if (proc && delta > 0) {
    proc_lock();
    if (from_state == EOS_TASK_STATE_SYS) {
      proc->sys_us += delta;
    } else if (from_state == EOS_TASK_STATE_USER) {
      proc->user_us += delta;
    }
    proc->cpu_time_us = proc->user_us + proc->sys_us;
    proc_unlock();
  }

  return prev;
}

void eos_proc_leave_wait(bool prev) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return;
  }
  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (!task) return;

  eos_proc_t *proc = NULL;
  uint64_t wait_delta = 0;

  task_flags_lock();
  if (s_task_flags) {
    eos_task_flags_t *entry = get_task_flags_locked(task, false);
    if (entry) {
      if (entry->wait_depth > 0) {
        entry->wait_depth--;
      }
      if (!prev) {
        entry->wait_depth = 0;
      }
      if (entry->wait_depth == 0) {
        uint64_t now = (uint64_t)esp_timer_get_time();
        if (entry->state == EOS_TASK_STATE_WAIT && entry->last_state_time > 0) {
          wait_delta = (now >= entry->last_state_time) ? (now - entry->last_state_time) : 0;
          proc = entry->proc;
        }
        entry->state = (entry->kernel_depth > 0) ? EOS_TASK_STATE_SYS : EOS_TASK_STATE_USER;
        entry->last_state_time = now;
      }
      release_task_flags_locked(entry);
    }
  }
  task_flags_unlock();

  if (proc && wait_delta > 0) {
    proc_lock();
    proc->wait_us += wait_delta;
    proc_unlock();
  }
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
  bool is_k = (entry != NULL && entry->kernel_depth > 0);
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
  uint32_t depth = entry ? entry->heap_dis_depth : 0;
  bool prev = (depth > 0);
  if (entry) {
    entry->heap_dis_depth = depth + 1;
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
    if (entry->heap_dis_depth > 0) {
      entry->heap_dis_depth--;
    }
    if (!prev) {
      entry->heap_dis_depth = 0;
    }
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
  bool disabled = (entry != NULL && entry->heap_dis_depth > 0);
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
  bool ret = (entry != NULL && entry->in_heap_wrap != 0);
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
    entry->in_heap_wrap = val ? 1 : 0;
    release_task_flags_locked(entry);
  }
  task_flags_unlock();
}

eos_proc_t *eos_proc_kernel(void) {
  return s_proc_table ? s_proc_table->kernel_proc : NULL;
}

eos_proc_t *eos_proc_current(void) {
  eos_proc_t *kproc = s_proc_table ? s_proc_table->kernel_proc : NULL;
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return kproc;
  }

  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (task == NULL) {
    return kproc;
  }

  if (eos_proc_task_is_system()) {
    return NULL;
  }

  task_flags_lock();
  eos_task_flags_t *entry = get_task_flags_locked(task, false);
  eos_proc_t *p = entry ? entry->proc : NULL;
  task_flags_unlock();

  if (p && p->in_use && p->state != EOS_PROC_STATE_ZOMBIE) {
    return p;
  }

  if (s_proc_table) {
    size_t count = kv_size(s_proc_table->procs);
    // 1. Check if this task is the main thread of an active process (reverse order for child priority)
    for (int i = (int)count - 1; i >= 0; i--) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use && proc->state != EOS_PROC_STATE_ZOMBIE &&
          proc->main_thread == task) {
        eos_proc_task_set_proc(task, proc);
        return proc;
      }
    }

    // 2. Check if this task is registered in any process's threads list
    for (int i = (int)count - 1; i >= 0; i--) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (!proc || !proc->in_use || proc->state == EOS_PROC_STATE_ZOMBIE) continue;
      for (size_t j = 0; j < kv_size(proc->threads); j++) {
        if (kv_A(proc->threads, j) == task) {
          eos_proc_task_set_proc(task, proc);
          return proc;
        }
      }
    }
  }

  return NULL;
}

typedef struct {
  SemaphoreHandle_t raw_sem;
  int type;
} esp_pthread_mutex_raw_t;

static void proc_destroy_mutex(pthread_mutex_t *mutex) {
  if (!mutex || !*mutex) return;
  if (pthread_mutex_destroy(mutex) != 0) {
    esp_pthread_mutex_raw_t *mux = (esp_pthread_mutex_raw_t *)*mutex;
    if (mux) {
      if (mux->raw_sem) {
        vSemaphoreDelete(mux->raw_sem);
      }
      free(mux);
    }
  }
  *mutex = 0;
}

static void proc_destroy_sync_primitives(eos_proc_t *p) {
  proc_destroy_mutex(&p->mem_mutex);
  proc_destroy_mutex(&p->fd_mutex);
  proc_destroy_mutex(&p->cleanup_mutex);
  if (p->wait_cond) {
    pthread_cond_broadcast(&p->wait_cond);
    pthread_cond_destroy(&p->wait_cond);
    p->wait_cond = 0;
  }
}

static void proc_init_sync_primitives(eos_proc_t *p) {
  pthread_mutexattr_t m_attr;
  pthread_mutexattr_init(&m_attr);
  pthread_mutexattr_settype(&m_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&p->mem_mutex, &m_attr);
  pthread_mutex_init(&p->fd_mutex, &m_attr);
  pthread_mutex_init(&p->cleanup_mutex, &m_attr);
  pthread_mutexattr_destroy(&m_attr);
  pthread_cond_init(&p->wait_cond, NULL);
}

static eos_proc_t *proc_alloc_initial_context(pid_t pid, pid_t ppid, pid_t pgid, pid_t sid, const char *name) {
  eos_proc_t *p = (eos_proc_t *)eos_psram_calloc_prefer(1, sizeof(eos_proc_t));
  if (!p) {
    p = (eos_proc_t *)calloc(1, sizeof(eos_proc_t));
  }
  if (!p) {
    return NULL;
  }
  p->pid = pid;
  p->ppid = ppid;
  p->pgid = pgid;
  p->sid = sid;
  p->state = EOS_PROC_STATE_RUNNING;
  p->start_time = esp_timer_get_time();
  p->in_use = 1;
  strlcpy(p->name, name, sizeof(p->name));
  strlcpy(p->cwd, "/", sizeof(p->cwd));
  strlcpy(p->tty, "/dev/console", sizeof(p->tty));

  kv_init(p->fds);
  kv_init(p->dirs);
  kv_init(p->memblocks);
  kv_init(p->env);
  kv_init(p->threads);
  kv_init(p->fd_map);
  kv_init(p->cleanup_hooks);
  p->next_hook_id = 1;

  proc_init_sync_primitives(p);

  return p;
}

ESP_EVENT_DEFINE_BASE(EOS_SYS_EVENT);
enum { EOS_SYS_EVENT_REBOOT = 1 };
static bool s_sys_evt_ready = false;

static void eos_sys_event_handler(void *arg, esp_event_base_t base,
                                  int32_t id, void *data) {
  (void)arg;
  (void)base;
  (void)data;
  if (id == EOS_SYS_EVENT_REBOOT) {
    eos_dev_detach_all();
    esp_restart();
  }
}

static void eos_sys_events_init(void) {
  if (s_sys_evt_ready) return;
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    EOS_LOGW("eos_sys_events_init: default event loop failed: %d", (int)err);
    return;
  }
  err = esp_event_handler_instance_register(EOS_SYS_EVENT, EOS_SYS_EVENT_REBOOT,
                                            eos_sys_event_handler, NULL, NULL);
  if (err != ESP_OK) {
    EOS_LOGW("eos_sys_events_init: handler register failed: %d", (int)err);
    return;
  }
  s_sys_evt_ready = true;
}

void eos_proc_init(void) {
  task_flags_lock();
  if (!s_task_flags) {
    s_task_flags = (eos_task_flags_t *)heap_caps_calloc(EOS_MAX_TASK_FLAGS, sizeof(eos_task_flags_t), MALLOC_CAP_INTERNAL);
  }
  task_flags_unlock();

  static pthread_mutex_t s_init_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&s_init_mutex);
  if (s_proc_table != NULL) {
    pthread_mutex_unlock(&s_init_mutex);
    return;
  }

  eos_proc_table_t *table = (eos_proc_table_t *)eos_psram_calloc_prefer(1, sizeof(eos_proc_table_t));
  if (!table) {
    table = (eos_proc_table_t *)calloc(1, sizeof(eos_proc_table_t));
  }
  if (!table) {
    pthread_mutex_unlock(&s_init_mutex);
    EOS_LOGE("Failed to allocate process table!\n");
    return;
  }

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&table->table_lock, &attr);
  pthread_mutexattr_destroy(&attr);
  pthread_cond_init(&table->cond, NULL);
  table->next_pid = 2;
  kv_init(table->procs);

  table->kernel_proc = proc_alloc_initial_context(0, 0, 0, 0, "kernel");
  if (!table->kernel_proc) {
    pthread_mutex_unlock(&s_init_mutex);
    EOS_LOGE("Failed to allocate kernel process context!\n");
    return;
  }

  eos_proc_t *init_proc = proc_alloc_initial_context(1, 0, 1, 1, "init");
  if (!init_proc) {
    pthread_mutex_unlock(&s_init_mutex);
    EOS_LOGE("Failed to allocate init process context!\n");
    return;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_proc_init_env(init_proc);

  init_proc->main_thread = xTaskGetCurrentTaskHandle();
  kv_push(TaskHandle_t, init_proc->threads, init_proc->main_thread);
  eos_proc_task_set_proc(init_proc->main_thread, init_proc);
  kv_push(eos_proc_t *, table->procs, init_proc);
  eos_proc_heap_tracking_restore(prev_h);

  s_proc_table = table;
  pthread_mutex_unlock(&s_init_mutex);
  eos_sys_events_init();
  EOS_LOGI("Process table initialized in PSRAM (dynamic kvec storage)\n");
}

static eos_proc_t *find_or_alloc_proc_slot_locked(void) {
  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *slot = kv_A(s_proc_table->procs, i);
    if (slot && !slot->in_use) {
      return slot;
    }
  }

  eos_proc_t *p = (eos_proc_t *)eos_psram_calloc_prefer(1, sizeof(eos_proc_t));
  if (!p) {
    p = (eos_proc_t *)calloc(1, sizeof(eos_proc_t));
  }
  if (!p) {
    return NULL;
  }

  kv_push(eos_proc_t *, s_proc_table->procs, p);
  return p;
}

static void proc_inherit_env_locked(eos_proc_t *p, eos_proc_t *cur) {
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
}

pid_t eos_proc_create(const char *name, const char *cmdline, pid_t ppid, pid_t pgid) {
  if (!s_proc_table) {
    eos_proc_init();
  }
  if (!s_proc_table) {
    errno = ENOMEM;
    return -1;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  proc_lock();

  eos_proc_t *p = find_or_alloc_proc_slot_locked();
  if (!p) {
    proc_unlock();
    eos_proc_heap_tracking_restore(prev_h);
    EOS_LOGE("Failed to allocate process record!\n");
    errno = ENOMEM;
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

  memset(p, 0, sizeof(eos_proc_t));
  proc_init_sync_primitives(p);
  p->next_hook_id = 1;
  kv_init(p->cleanup_hooks);

  p->pid = new_pid;
  p->ppid = (ppid > 0) ? ppid : (cur ? cur->pid : 1);
  p->pgid = (pgid > 0) ? pgid : new_pid;
  p->sid = (cur ? cur->sid : new_pid);
  p->state = EOS_PROC_STATE_RUNNING;
  p->start_time = esp_timer_get_time();
  p->in_use = 1;

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

  p->cmdline = cmdline ? eos_psram_strdup(cmdline) : NULL;
  strlcpy(p->tty, "/dev/console", sizeof(p->tty));

  kv_init(p->fds);
  kv_init(p->dirs);
  kv_init(p->memblocks);
  kv_init(p->env);
  kv_init(p->threads);
  kv_init(p->fd_map);

  proc_inherit_env_locked(p, cur);
  if (cur) {
    for (int fd = 0; fd <= STDERR_FILENO; fd++) {
      int mapped = eos_proc_fd_map_get(cur, fd);
      if (mapped >= 0) {
        eos_proc_fd_map_set(p, fd, mapped);
      }
    }
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
    size_t count = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < count; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (!proc || !proc->in_use) continue;
      if (proc->pid != pid) {
        if (proc->main_thread == thread) {
          proc->main_thread = NULL;
        }
        for (size_t j = 0; j < kv_size(proc->threads); j++) {
          if (kv_A(proc->threads, j) == thread) {
            kv_drop_fast(TaskHandle_t, proc->threads, j);
            break;
          }
        }
      }
    }
    for (size_t i = 0; i < count; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use && proc->pid == pid) {
        proc->main_thread = thread;
        eos_proc_add_thread_locked(proc, thread);
        eos_proc_task_set_proc(thread, proc);
        break;
      }
    }
  }
  proc_unlock();
}

void eos_proc_set_state(pid_t pid, eos_proc_state_t state) {
  proc_lock();
  if (s_proc_table) {
    size_t count = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < count; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use && proc->pid == pid) {
        proc->state = state;
        break;
      }
    }
  }
  proc_unlock();
}

void eos_proc_set_tty(pid_t pid, const char *tty_name) {
  proc_lock();
  if (s_proc_table && tty_name) {
    size_t count = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < count; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use && proc->pid == pid) {
        strlcpy(proc->tty, tty_name, sizeof(proc->tty));
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
    size_t count = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < count; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use && proc->pid == pid) {
        strlcpy(proc->cwd, cwd, sizeof(proc->cwd));
        break;
      }
    }
  }
  proc_unlock();
}

static eos_proc_t *proc_find_locked(pid_t pid) {
  if (!s_proc_table) return NULL;
  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *proc = kv_A(s_proc_table->procs, i);
    if (proc && proc->in_use && proc->pid == pid) {
      return proc;
    }
  }
  return NULL;
}

static void proc_update_cpu_time_locked(eos_proc_t *p) {
  task_flags_lock();
  if (s_task_flags) {
    uint64_t now = (uint64_t)esp_timer_get_time();
    for (int i = 0; i < EOS_MAX_TASK_FLAGS; i++) {
      if (s_task_flags[i].task && s_task_flags[i].proc == p && s_task_flags[i].last_state_time > 0) {
        uint64_t delta = (now >= s_task_flags[i].last_state_time) ? (now - s_task_flags[i].last_state_time) : 0;
        if (delta > 0) {
          if (s_task_flags[i].state == EOS_TASK_STATE_USER) {
            p->user_us += delta;
          } else if (s_task_flags[i].state == EOS_TASK_STATE_SYS) {
            p->sys_us += delta;
          } else if (s_task_flags[i].state == EOS_TASK_STATE_WAIT) {
            p->wait_us += delta;
          }
          s_task_flags[i].last_state_time = now;
        }
      }
    }
  }
  task_flags_unlock();
  p->cpu_time_us = p->user_us + p->sys_us;
}

static bool proc_drain_cleanup_hooks(pid_t pid) {
  while (1) {
    eos_proc_t *p = proc_find_locked(pid);
    if (!p) return false;
    pthread_mutex_lock(&p->cleanup_mutex);
    if (kv_size(p->cleanup_hooks) == 0) {
      pthread_mutex_unlock(&p->cleanup_mutex);
      return true;
    }
    eos_proc_cleanup_hook_t hook = kv_pop(p->cleanup_hooks);
    pthread_mutex_unlock(&p->cleanup_mutex);
    proc_unlock();

    if (hook.fn) {
      hook.fn(hook.arg);
    }
    proc_lock();
  }
}

static void proc_close_fds_and_dirs(eos_proc_t *p) {
  int fds_to_close[EOS_LINE_STR_LEN];
  size_t num_fds = 0;
  pthread_mutex_lock(&p->fd_mutex);
  while (kv_size(p->fds) > 0 && num_fds < EOS_LINE_STR_LEN) {
    fds_to_close[num_fds++] = kv_pop(p->fds);
  }
  DIR *dirs_to_close[EOS_XSMALL_STR_LEN];
  size_t num_dirs = 0;
  while (kv_size(p->dirs) > 0 && num_dirs < EOS_XSMALL_STR_LEN) {
    dirs_to_close[num_dirs++] = kv_pop(p->dirs);
  }
  for (size_t i = 0; i < kv_size(p->fd_map); i++) {
    kv_A(p->fd_map, i) = -1;
  }
  pthread_mutex_unlock(&p->fd_mutex);

  proc_unlock();
  for (size_t j = 0; j < num_fds; j++) {
    close(fds_to_close[j]);
  }
  for (size_t j = 0; j < num_dirs; j++) {
    closedir(dirs_to_close[j]);
  }
  proc_lock();
}

static void proc_reap_background_threads(eos_proc_t *p) {
  bool has_other_threads = false;
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  for (size_t t = 0; t < kv_size(p->threads); t++) {
    TaskHandle_t th = kv_A(p->threads, t);
    if (th && th != self) {
      has_other_threads = true;
      EOS_LOGW("proc: process '%s' (pid %d) leaked thread %p ('%s') - killing\n",
               p->name, p->pid, th, pcTaskGetName(th));
    }
  }
  if (has_other_threads) {
    proc_unlock();
    bool prev_w = eos_proc_enter_wait();
    vTaskDelay(pdMS_TO_TICKS(10));
    eos_proc_leave_wait(prev_w);
    proc_lock();
  }
}

static void proc_terminate_threads_locked(eos_proc_t *p) {
  TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
  for (size_t j = 0; j < kv_size(p->threads); j++) {
    TaskHandle_t th = kv_A(p->threads, j);
    if (th && th != cur_task) {
      eTaskState ts = eTaskGetState(th);
      if (ts != eDeleted && ts != eInvalid) {
        eos_proc_task_flags_cleanup(th);
        vTaskDelete(th);
      }
    }
  }
  kv_destroy(p->threads);
  kv_init(p->threads);
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

  eos_proc_t *p = proc_find_locked(pid);
  if (!p || p->state == EOS_PROC_STATE_ZOMBIE) {
    proc_unlock();
    return;
  }

  proc_update_cpu_time_locked(p);

  if (!proc_drain_cleanup_hooks(pid)) {
    proc_unlock();
    return;
  }

  p = proc_find_locked(pid);
  if (!p) {
    proc_unlock();
    return;
  }

  proc_close_fds_and_dirs(p);

  p = proc_find_locked(pid);
  if (!p) {
    proc_unlock();
    return;
  }

  proc_reap_background_threads(p);

  p = proc_find_locked(pid);
  if (!p) {
    proc_unlock();
    return;
  }

  proc_terminate_threads_locked(p);

  p->exit_code = exit_code;
  p->last_exit_status = exit_code;
  p->state = EOS_PROC_STATE_ZOMBIE;
  p->main_thread = NULL;

  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *child = kv_A(s_proc_table->procs, i);
    if (child && child->in_use && child->ppid == pid) {
      child->ppid = 1;
    }
  }

  pthread_cond_broadcast(&p->wait_cond);
  pthread_cond_broadcast(&s_proc_table->cond);
  proc_unlock();
}

eos_proc_t *eos_proc_find(pid_t pid) {
  if (pid == 0) return s_proc_table ? s_proc_table->kernel_proc : NULL;
  if (!s_proc_table) return NULL;
  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *proc = kv_A(s_proc_table->procs, i);
    if (proc && proc->in_use && proc->pid == pid) {
      return proc;
    }
  }
  return NULL;
}

bool eos_proc_is_alive(pid_t pid) {
  if (pid == 0) return true;
  eos_proc_t *p = eos_proc_find(pid);
  return (p && p->in_use && p->state != EOS_PROC_STATE_ZOMBIE);
}

bool eos_proc_get_copy(pid_t pid, eos_proc_t *out_proc) {
  if (!out_proc) return false;
  if (pid == 0) {
    if (!s_proc_table || !s_proc_table->kernel_proc) return false;
    pthread_mutex_lock(&s_proc_table->kernel_proc->mem_mutex);
    memcpy(out_proc, s_proc_table->kernel_proc, sizeof(eos_proc_t));
    pthread_mutex_unlock(&s_proc_table->kernel_proc->mem_mutex);
    return true;
  }
  if (!s_proc_table) return false;
  proc_lock();
  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *proc = kv_A(s_proc_table->procs, i);
    if (proc && proc->in_use && proc->pid == pid) {
      memcpy(out_proc, proc, sizeof(eos_proc_t));
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
    size_t total = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < total && count < max_pids; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use) {
        out_pids[count++] = proc->pid;
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
      size_t count = kv_size(s_proc_table->procs);
      for (size_t i = 0; i < count; i++) {
        eos_proc_t *proc = kv_A(s_proc_table->procs, i);
        if (proc && proc->in_use && proc->pid == pid) {
          pgid = proc->pgid;
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

  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *proc = kv_A(s_proc_table->procs, i);
    if (proc && proc->in_use && proc->pid == pid) {
      proc->pgid = pgid;
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

  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *proc = kv_A(s_proc_table->procs, i);
    if (proc && proc->in_use && proc->pid == pid) {
      proc->sid = pid;
      proc->pgid = pid;
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
    size_t total = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < total; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use) {
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
    size_t total = kv_size(s_proc_table->procs);
    for (size_t i = 0; i < total && written < max_count; i++) {
      eos_proc_t *proc = kv_A(s_proc_table->procs, i);
      if (proc && proc->in_use) {
        memcpy(&out_table[written], proc, sizeof(eos_proc_t));
        written++;
      }
    }
  }
  proc_unlock();
  return written;
}

int eos_proc_on_exit_pid(pid_t pid, eos_proc_cleanup_fn_t fn, void *arg, const char *desc) {
  if (!fn) return -1;
  proc_lock();
  if (!s_proc_table) {
    proc_unlock();
    return -1;
  }
  eos_proc_t *p = NULL;
  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *proc = kv_A(s_proc_table->procs, i);
    if (proc && proc->in_use && proc->pid == pid) {
      p = proc;
      break;
    }
  }
  if (!p || p->state == EOS_PROC_STATE_ZOMBIE || p->state == EOS_PROC_STATE_UNUSED) {
    proc_unlock();
    return -1;
  }
  pthread_mutex_lock(&p->cleanup_mutex);
  uint32_t hook_id = p->next_hook_id++;
  if (p->next_hook_id == 0) p->next_hook_id = 1;
  eos_proc_cleanup_hook_t hook = {
    .hook_id = hook_id,
    .fn = fn,
    .arg = arg,
    .desc = desc,
  };
  bool prev_h = eos_proc_heap_tracking_disable();
  kv_push(eos_proc_cleanup_hook_t, p->cleanup_hooks, hook);
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&p->cleanup_mutex);
  proc_unlock();
  return (int)hook_id;
}

int eos_proc_cancel_exit_pid(pid_t pid, int hook_id) {
  if (hook_id <= 0) return -1;
  proc_lock();
  if (!s_proc_table) {
    proc_unlock();
    return -1;
  }
  eos_proc_t *p = NULL;
  size_t count = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < count; i++) {
    eos_proc_t *proc = kv_A(s_proc_table->procs, i);
    if (proc && proc->in_use && proc->pid == pid) {
      p = proc;
      break;
    }
  }
  if (!p) {
    proc_unlock();
    return -1;
  }
  pthread_mutex_lock(&p->cleanup_mutex);
  int ret = -1;
  for (size_t i = 0; i < kv_size(p->cleanup_hooks); i++) {
    if (kv_A(p->cleanup_hooks, i).hook_id == (uint32_t)hook_id) {
      for (size_t j = i; j + 1 < kv_size(p->cleanup_hooks); j++) {
        kv_A(p->cleanup_hooks, j) = kv_A(p->cleanup_hooks, j + 1);
      }
      p->cleanup_hooks.n--;
      ret = 0;
      break;
    }
  }
  pthread_mutex_unlock(&p->cleanup_mutex);
  proc_unlock();
  return ret;
}

int eos_proc_cancel_exit_match(pid_t pid, eos_proc_cleanup_fn_t fn, void *arg) {
  if (!fn) return -1;
  proc_lock();
  if (!s_proc_table) {
    proc_unlock();
    return -1;
  }

  // If pid == 0 or target matches current process, try current process first
  eos_proc_t *cur = eos_proc_current();
  if ((pid == 0 && cur && cur->pid > 0) || (pid > 0 && cur && cur->pid == pid)) {
    pthread_mutex_lock(&cur->cleanup_mutex);
    for (size_t i = 0; i < kv_size(cur->cleanup_hooks); i++) {
      eos_proc_cleanup_hook_t *h = &kv_A(cur->cleanup_hooks, i);
      if (h->fn == fn && h->arg == arg) {
        for (size_t j = i; j + 1 < kv_size(cur->cleanup_hooks); j++) {
          kv_A(cur->cleanup_hooks, j) = kv_A(cur->cleanup_hooks, j + 1);
        }
        cur->cleanup_hooks.n--;
        pthread_mutex_unlock(&cur->cleanup_mutex);
        proc_unlock();
        return 0;
      }
    }
    pthread_mutex_unlock(&cur->cleanup_mutex);
  }

  // If not found in current process:
  size_t count = kv_size(s_proc_table->procs);
  for (size_t pidx = 0; pidx < count; pidx++) {
    eos_proc_t *p = kv_A(s_proc_table->procs, pidx);
    if (!p || !p->in_use || (pid > 0 && p->pid != pid) || (pid == 0 && p == cur)) continue;
    pthread_mutex_lock(&p->cleanup_mutex);
    for (size_t i = 0; i < kv_size(p->cleanup_hooks); i++) {
      eos_proc_cleanup_hook_t *h = &kv_A(p->cleanup_hooks, i);
      if (h->fn == fn && h->arg == arg) {
        for (size_t j = i; j + 1 < kv_size(p->cleanup_hooks); j++) {
          kv_A(p->cleanup_hooks, j) = kv_A(p->cleanup_hooks, j + 1);
        }
        p->cleanup_hooks.n--;
        pthread_mutex_unlock(&p->cleanup_mutex);
        proc_unlock();
        return 0;
      }
    }
    pthread_mutex_unlock(&p->cleanup_mutex);
  }

  proc_unlock();
  return -1;
}

int eos_proc_on_exit(eos_proc_cleanup_fn_t fn, void *arg, const char *desc) {
  eos_proc_t *cur = eos_proc_current();
  if (!cur) return -1;
  return eos_proc_on_exit_pid(cur->pid, fn, arg, desc);
}

int eos_proc_cancel_exit(int hook_id) {
  eos_proc_t *cur = eos_proc_current();
  if (!cur) return -1;
  return eos_proc_cancel_exit_pid(cur->pid, hook_id);
}

static int find_matching_child_locked(pid_t pid, pid_t calling_pid, pid_t calling_pgid, bool *has_matching) {
  *has_matching = false;
  int zombie_slot = -1;
  size_t total = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < total; i++) {
    eos_proc_t *p = kv_A(s_proc_table->procs, i);
    if (!p || !p->in_use) continue;

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
      *has_matching = true;
      if (p->state == EOS_PROC_STATE_ZOMBIE) {
        zombie_slot = (int)i;
        break;
      }
    }
  }
  return zombie_slot;
}

static void proc_reap_zombie_locked(eos_proc_t *z) {
  extern void __real_heap_caps_free(void *ptr);
  extern int __real__close_r(struct _reent *r, int fd);
  extern int __real_closedir(DIR *pdir);

  int fds_to_close[EOS_LINE_STR_LEN];
  size_t num_fds = 0;
  pthread_mutex_lock(&z->fd_mutex);
  while (kv_size(z->fds) > 0 && num_fds < EOS_LINE_STR_LEN) {
    fds_to_close[num_fds++] = kv_pop(z->fds);
  }
  DIR *dirs_to_close[EOS_XSMALL_STR_LEN];
  size_t num_dirs = 0;
  while (kv_size(z->dirs) > 0 && num_dirs < EOS_XSMALL_STR_LEN) {
    dirs_to_close[num_dirs++] = kv_pop(z->dirs);
  }
  pthread_mutex_unlock(&z->fd_mutex);

  proc_unlock();
  for (size_t j = 0; j < num_fds; j++) {
    int fd = fds_to_close[j];
    if (fd >= 0) {
      eos_vfs_file_t *f = eos_vfs_file_close_start(fd);
      __real__close_r(__getreent(), fd);
      if (f) {
        eos_vfs_file_release(f);
      }
    }
  }

  for (size_t j = 0; j < num_dirs; j++) {
    DIR *d = dirs_to_close[j];
    if (d) {
      proc_memblock_remove_locked(z, (void *)d, NULL, NULL);
      eos_vfs_dir_t *dir = eos_vfs_dir_close_start(d);
      __real_closedir(d);
      if (dir) {
        eos_vfs_dir_release(dir);
      }
    }
  }
  proc_lock();

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

  proc_memblock_remove_locked(z, z->fds.a, NULL, NULL);
  proc_memblock_remove_locked(z, z->dirs.a, NULL, NULL);
  proc_memblock_remove_locked(z, z->env.a, NULL, NULL);
  proc_memblock_remove_locked(z, z->threads.a, NULL, NULL);
  proc_memblock_remove_locked(z, z->fd_map.a, NULL, NULL);
  proc_memblock_remove_locked(z, z->cleanup_hooks.a, NULL, NULL);
  proc_memblock_remove_locked(z, z->memblocks.a, NULL, NULL);

  for (size_t j = 0; j < kv_size(z->memblocks); j++) {
    void *b = kv_A(z->memblocks, j).ptr;
    if (b && !kv_owns_buffer(z->memblocks, b) &&
        !kv_owns_buffer(z->fds, b) &&
        !kv_owns_buffer(z->dirs, b) &&
        !kv_owns_buffer(z->env, b) &&
        !kv_owns_buffer(z->threads, b) &&
        !kv_owns_buffer(z->fd_map, b) &&
        !kv_owns_buffer(z->cleanup_hooks, b)) {
      __real_heap_caps_free(b);
    }
  }

  kv_destroy(z->fds);
  kv_destroy(z->dirs);
  kv_destroy(z->env);
  kv_destroy(z->threads);
  kv_destroy(z->fd_map);
  kv_destroy(z->cleanup_hooks);
  kv_destroy(z->memblocks);

  proc_destroy_sync_primitives(z);
  memset(z, 0, sizeof(eos_proc_t));
  z->in_use = false;
  z->state = EOS_PROC_STATE_UNUSED;
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
    int zombie_slot = find_matching_child_locked(pid, calling_pid, calling_pgid, &has_matching_child);

    if (zombie_slot >= 0) {
      eos_proc_t *z = kv_A(s_proc_table->procs, zombie_slot);
      pid_t reaped_pid = z->pid;
      int raw_status = (z->exit_code & 0xff) << 8;

      if (status) {
        *status = raw_status;
      }

      if (cur) {
        cur->cutime_us += z->user_us + z->cutime_us;
        cur->cstime_us += z->sys_us + z->cstime_us;
        cur->cwait_us += z->wait_us + z->cwait_us;
      }

      bool prev_h = eos_proc_heap_tracking_disable();
      proc_reap_zombie_locked(z);
      eos_proc_heap_tracking_restore(prev_h);

      proc_unlock();
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

    bool prev_w = eos_proc_enter_wait();
    pthread_cond_wait(&s_proc_table->cond, &s_proc_table->table_lock);
    eos_proc_leave_wait(prev_w);
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
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext() || !xPortCanYield()) return;

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
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext() || !xPortCanYield()) return;
  if (eos_proc_task_is_system() || eos_proc_heap_tracking_is_disabled()) return;

  if (proc && proc->in_use) {
    pthread_mutex_lock(&proc->mem_mutex);
    bool removed = proc_memblock_remove_locked(proc, ptr, NULL, NULL);
    pthread_mutex_unlock(&proc->mem_mutex);
    if (removed) return;
  }

  // Fallback: check PID 0 (kernel context)
  if (s_proc_table && s_proc_table->kernel_proc && proc != s_proc_table->kernel_proc) {
    pthread_mutex_lock(&s_proc_table->kernel_proc->mem_mutex);
    bool removed = proc_memblock_remove_locked(s_proc_table->kernel_proc, ptr, NULL, NULL);
    pthread_mutex_unlock(&s_proc_table->kernel_proc->mem_mutex);
    if (removed) return;
  }

  // If caller has no process context or process table uninitialized, skip scan
  if (!proc || !proc->in_use || !s_proc_table) return;

  // Fallback: search other active processes (cross-process frees)
  // Skip any process slot that is unused, zombie, or has 0 tracked allocations
  proc_lock();
  size_t total = kv_size(s_proc_table->procs);
  for (size_t i = 0; i < total; i++) {
    eos_proc_t *p = kv_A(s_proc_table->procs, i);
    if (p && p->in_use && p->state != EOS_PROC_STATE_ZOMBIE &&
        p != proc && p != s_proc_table->kernel_proc) {
      if (p->memblocks.n == 0) continue;
      pthread_mutex_lock(&p->mem_mutex);
      bool found = proc_memblock_remove_locked(p, ptr, NULL, NULL);
      pthread_mutex_unlock(&p->mem_mutex);
      if (found) {
        proc_unlock();
        return;
      }
    }
  }
  proc_unlock();
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

  proc_lock();
  eos_proc_t *target = eos_proc_find(target_pid);
  if (!target || !target->in_use || target->state == EOS_PROC_STATE_ZOMBIE) {
    proc_unlock();
    errno = ESRCH;
    return -1;
  }

  eos_proc_t *current = eos_proc_current();
  size_t size = 0;
  bool is_psram = false;
  bool found = false;

  // Authorization: Only the owning process or kernel context (PID 0) can adopt out a block
  if (current && current->in_use && current->state != EOS_PROC_STATE_ZOMBIE) {
    pthread_mutex_lock(&current->mem_mutex);
    found = proc_memblock_remove_locked(current, ptr, &size, &is_psram);
    pthread_mutex_unlock(&current->mem_mutex);
  }

  // If not found in current process, allow adopting from kernel process context (PID 0)
  if (!found && s_proc_table && s_proc_table->kernel_proc && current != s_proc_table->kernel_proc) {
    pthread_mutex_lock(&s_proc_table->kernel_proc->mem_mutex);
    found = proc_memblock_remove_locked(s_proc_table->kernel_proc, ptr, &size, &is_psram);
    pthread_mutex_unlock(&s_proc_table->kernel_proc->mem_mutex);
  }

  if (!found) {
    proc_unlock();
    errno = ENOENT;
    return -1;
  }

  eos_proc_mem_insert(target, ptr, size, is_psram);
  proc_unlock();
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
  pthread_mutex_lock(&proc->fd_mutex);
  bool prev_h = eos_proc_heap_tracking_disable();
  kv_push(int, proc->fds, fd);
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&proc->fd_mutex);
}

void eos_proc_unreg_fd(int fd) {
  if (fd < 0) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;

  pthread_mutex_lock(&proc->fd_mutex);
  bool prev_h = eos_proc_heap_tracking_disable();
  for (size_t i = 0; i < kv_size(proc->fds); i++) {
    if (kv_A(proc->fds, i) == fd) {
      kv_drop_fast(int, proc->fds, i);
      break;
    }
  }
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&proc->fd_mutex);
}

void eos_proc_reg_dir(DIR *dir) {
  if (!dir) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;
  pthread_mutex_lock(&proc->fd_mutex);
  bool prev_h = eos_proc_heap_tracking_disable();
  kv_push(DIR *, proc->dirs, dir);
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&proc->fd_mutex);
}

void eos_proc_unreg_dir(DIR *dir) {
  if (!dir) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;

  pthread_mutex_lock(&proc->fd_mutex);
  bool prev_h = eos_proc_heap_tracking_disable();
  for (size_t i = 0; i < kv_size(proc->dirs); i++) {
    if (kv_A(proc->dirs, i) == dir) {
      kv_drop_fast(DIR *, proc->dirs, i);
      break;
    }
  }
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&proc->fd_mutex);
}

eos_proc_fd_snapshot_t eos_proc_snapshot_fds(void) {
  eos_proc_fd_snapshot_t snap = {0};
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return snap;

  pthread_mutex_lock(&proc->fd_mutex);
  snap.valid = true;
  size_t n_fds = kv_size(proc->fds);
  if (n_fds > 0) {
    snap.fds = eos_psram_malloc_prefer(n_fds * sizeof(int));
    if (snap.fds) {
      for (size_t j = 0; j < n_fds; j++) {
        snap.fds[j] = kv_A(proc->fds, j);
      }
      snap.count = n_fds;
    }
  }

  size_t n_dirs = kv_size(proc->dirs);
  if (n_dirs > 0) {
    snap.dirs = eos_psram_malloc_prefer(n_dirs * sizeof(DIR *));
    if (snap.dirs) {
      for (size_t j = 0; j < n_dirs; j++) {
        snap.dirs[j] = kv_A(proc->dirs, j);
      }
      snap.dir_count = n_dirs;
    }
  }
  pthread_mutex_unlock(&proc->fd_mutex);

  return snap;
}

void eos_proc_cleanup_leaked_fds(eos_proc_fd_snapshot_t *snap) {
  if (!snap || !snap->valid) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    if (snap->fds) eos_psram_free(snap->fds);
    if (snap->dirs) eos_psram_free(snap->dirs);
    snap->fds = NULL;
    snap->dirs = NULL;
    snap->count = 0;
    snap->dir_count = 0;
    snap->valid = false;
    return;
  }

  int fds_to_close[EOS_LINE_STR_LEN];
  size_t num_fds_to_close = 0;
  DIR *dirs_to_close[EOS_XSMALL_STR_LEN];
  size_t num_dirs_to_close = 0;

  pthread_mutex_lock(&proc->fd_mutex);
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
    if (!was_present && num_fds_to_close < EOS_LINE_STR_LEN) {
      fds_to_close[num_fds_to_close++] = fd;
    }
  }

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
    if (!was_present && num_dirs_to_close < EOS_XSMALL_STR_LEN) {
      dirs_to_close[num_dirs_to_close++] = dir;
    }
  }
  pthread_mutex_unlock(&proc->fd_mutex);

  for (size_t i = 0; i < num_fds_to_close; i++) {
    EOS_LOGW("Closing leaked fd %d\n", fds_to_close[i]);
    eos_proc_unreg_fd(fds_to_close[i]);
    close(fds_to_close[i]);
  }
  for (size_t i = 0; i < num_dirs_to_close; i++) {
    EOS_LOGW("Closing leaked dir %p\n", dirs_to_close[i]);
    eos_proc_unreg_dir(dirs_to_close[i]);
    closedir(dirs_to_close[i]);
  }

  if (snap->fds) {
    eos_psram_free(snap->fds);
    snap->fds = NULL;
  }
  if (snap->dirs) {
    eos_psram_free(snap->dirs);
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
  eos_proc_t *p_commit = NULL;
  uint64_t delta = 0;
  int state = -1;

  task_flags_lock();
  if (s_task_flags) {
    eos_task_flags_t *entry = get_task_flags_locked(thread, false);
    if (entry && entry->proc && entry->last_state_time > 0) {
      uint64_t now = (uint64_t)esp_timer_get_time();
      delta = (now >= entry->last_state_time) ? (now - entry->last_state_time) : 0;
      p_commit = entry->proc;
      state = (int)entry->state;
      entry->last_state_time = now;
    }
  }
  task_flags_unlock();

  proc_lock();
  if (p_commit && delta > 0) {
    if (state == EOS_TASK_STATE_USER) {
      p_commit->user_us += delta;
    } else if (state == EOS_TASK_STATE_SYS) {
      p_commit->sys_us += delta;
    } else if (state == EOS_TASK_STATE_WAIT) {
      p_commit->wait_us += delta;
    }
    p_commit->cpu_time_us = p_commit->user_us + p_commit->sys_us;
  }

  if (proc->state == EOS_PROC_STATE_ZOMBIE) {
    proc_unlock();
    eos_proc_heap_tracking_restore(prev_h);
    return;
  }
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

void eos_proc_get_cpu_times(eos_proc_t *proc, uint64_t *user_us, uint64_t *sys_us, uint64_t *wait_us) {
  if (!proc) {
    if (user_us) *user_us = 0;
    if (sys_us) *sys_us = 0;
    if (wait_us) *wait_us = 0;
    return;
  }

  uint64_t u_add = 0, s_add = 0, w_add = 0;

  task_flags_lock();
  if (s_task_flags) {
    uint64_t now = (uint64_t)esp_timer_get_time();
    for (int i = 0; i < EOS_MAX_TASK_FLAGS; i++) {
      if (s_task_flags[i].task && s_task_flags[i].proc == proc && s_task_flags[i].last_state_time > 0) {
        uint64_t delta = (now >= s_task_flags[i].last_state_time) ? (now - s_task_flags[i].last_state_time) : 0;
        if (delta > 0) {
          if (s_task_flags[i].state == EOS_TASK_STATE_USER) {
            u_add += delta;
          } else if (s_task_flags[i].state == EOS_TASK_STATE_SYS) {
            s_add += delta;
          } else if (s_task_flags[i].state == EOS_TASK_STATE_WAIT) {
            w_add += delta;
          }
          s_task_flags[i].last_state_time = now;
        }
      }
    }
  }
  task_flags_unlock();

  proc_lock();
  proc->user_us += u_add;
  proc->sys_us += s_add;
  proc->wait_us += w_add;
  proc->cpu_time_us = proc->user_us + proc->sys_us;

  uint64_t u = proc->user_us;
  uint64_t s = proc->sys_us;
  uint64_t w = proc->wait_us;
  proc_unlock();

  if (user_us) *user_us = u;
  if (sys_us) *sys_us = s;
  if (wait_us) *wait_us = w;
}

uint64_t eos_proc_get_cpu_time_us(eos_proc_t *proc) {
  uint64_t u = 0, s = 0, w = 0;
  eos_proc_get_cpu_times(proc, &u, &s, &w);
  return u + s;
}

eos_pwrap_t *eos_proc_wrap_prepare(void *thread_start, void *thread_data) {
  eos_proc_t *cur = eos_proc_current();
  if (!cur) {
    if (!s_proc_table) eos_proc_init();
    cur = (s_proc_table && kv_size(s_proc_table->procs) > 0) ? kv_A(s_proc_table->procs, 0) : NULL;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_pwrap_t *pwrap = eos_psram_calloc_prefer(1, sizeof(eos_pwrap_t));
  if (!pwrap) {
    pwrap = calloc(1, sizeof(eos_pwrap_t));
  }
  eos_proc_heap_tracking_restore(prev_h);
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

void eos_reboot(bool bootloader) {
  if (bootloader) {
    extern void eos_arch_force_download_boot(void);
    eos_arch_force_download_boot();
  }
  if (s_sys_evt_ready &&
      xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
      esp_event_post(EOS_SYS_EVENT, EOS_SYS_EVENT_REBOOT, NULL, 0, 0) == ESP_OK) {
    for (;;) {
      vTaskDelay(portMAX_DELAY);
    }
  }
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    eos_dev_detach_all();
  }
  esp_restart();
}

void eos_reboot_from_isr(bool bootloader) {
  if (bootloader) {
    extern void eos_arch_force_download_boot(void);
    eos_arch_force_download_boot();
  }
  if (s_sys_evt_ready) {
    BaseType_t woken = pdFALSE;
    if (esp_event_isr_post(EOS_SYS_EVENT, EOS_SYS_EVENT_REBOOT, NULL, 0, &woken) == ESP_OK) {
      portYIELD_FROM_ISR(woken);
      return;
    }
  }
  esp_restart();
}

bool eos_ktask_create_psram(eos_ktask_t *out, TaskFunction_t fn, const char *name,
                            uint32_t stack_bytes, void *arg, UBaseType_t prio) {
  if (!out || !fn || stack_bytes == 0) return false;
  memset(out, 0, sizeof(*out));

  extern TaskHandle_t __real_xTaskCreateStaticPinnedToCore(
      TaskFunction_t pxTaskCode, const char *const pcName,
      const uint32_t ulStackDepth, void *const pvParameters,
      UBaseType_t uxPriority, StackType_t *const pxStackBuffer,
      StaticTask_t *const pxTaskBuffer, const BaseType_t xCoreID);
  extern BaseType_t __real_xTaskCreatePinnedToCore(
      TaskFunction_t pxTaskCode, const char *const pcName,
      const configSTACK_DEPTH_TYPE usStackDepth, void *const pvParameters,
      UBaseType_t uxPriority, TaskHandle_t *const pvCreatedTask,
      const BaseType_t xCoreID);

  out->stack = (StackType_t *)eos_psram_malloc_prefer(stack_bytes * sizeof(StackType_t));
  out->tcb = (StaticTask_t *)heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (out->stack && out->tcb) {
    bool prev_h = eos_proc_heap_tracking_disable();
    out->handle = __real_xTaskCreateStaticPinnedToCore(
        fn, name, stack_bytes, arg, prio, out->stack, out->tcb, tskNO_AFFINITY);
    eos_proc_heap_tracking_restore(prev_h);
    if (out->handle) {
      eos_proc_task_classify(out->handle, name);
      return true;
    }
  }

  eos_ktask_cleanup(out);
  bool prev_h = eos_proc_heap_tracking_disable();
  BaseType_t rc = __real_xTaskCreatePinnedToCore(
      fn, name, stack_bytes, arg, prio, &out->handle, tskNO_AFFINITY);
  eos_proc_heap_tracking_restore(prev_h);
  if (rc == pdPASS && out->handle) {
    eos_proc_task_classify(out->handle, name);
    return true;
  }
  out->handle = NULL;
  return false;
}

void eos_ktask_cleanup(eos_ktask_t *kt) {
  if (!kt) return;
  kt->handle = NULL;
  if (kt->stack) {
    free(kt->stack);
    kt->stack = NULL;
  }
  if (kt->tcb) {
    free(kt->tcb);
    kt->tcb = NULL;
  }
}

typedef struct {
  SemaphoreHandle_t done_sem;
  bool ran;
  bool stack_in_psram;
} ktask_test_ctx_t;

static void ktask_test_fn(void *arg) {
  ktask_test_ctx_t *ctx = (ktask_test_ctx_t *)arg;
  volatile uint32_t stack_probe = 0;
  ctx->stack_in_psram = esp_ptr_external_ram((const void *)&stack_probe);
  ctx->ran = true;
  xSemaphoreGive(ctx->done_sem);
  vTaskDelete(NULL);
}

bool eos_ktask_self_test(void) {
  if (!s_sys_evt_ready) return false;
  ktask_test_ctx_t ctx = {
      .done_sem = xSemaphoreCreateBinary(),
      .ran = false,
      .stack_in_psram = false,
  };
  if (!ctx.done_sem) return false;

  eos_ktask_t kt;
  bool ok = eos_ktask_create_psram(&kt, ktask_test_fn, "ktask_tst",
                                   EOS_BIN_MIN_STACK_SIZE, &ctx, tskIDLE_PRIORITY + 1);
  if (ok) {
    ok = (xSemaphoreTake(ctx.done_sem, pdMS_TO_TICKS(EOS_POLL_INTERVAL_MS * EOS_POLL_INTERVAL_MS)) == pdTRUE) &&
         ctx.ran && ctx.stack_in_psram;
    vTaskDelay(pdMS_TO_TICKS(EOS_POLL_INTERVAL_MS));
  }
  eos_ktask_cleanup(&kt);
  vSemaphoreDelete(ctx.done_sem);
  return ok && kt.handle == NULL && kt.stack == NULL && kt.tcb == NULL;
}

static void proc_disown_reent_blocks(eos_proc_t *proc, struct _reent *r) {
#if CONFIG_LIBC_NEWLIB
  if (!proc || !r || r == _GLOBAL_REENT) return;
  pthread_mutex_lock(&proc->mem_mutex);
#ifdef _REENT_SMALL
  if (r->_mp) {
    if (r->_mp->_result) proc_memblock_remove_locked(proc, r->_mp->_result, NULL, NULL);
    if (r->_mp->_p5s) proc_memblock_remove_locked(proc, r->_mp->_p5s, NULL, NULL);
    if (r->_mp->_freelist) proc_memblock_remove_locked(proc, (void *)r->_mp->_freelist, NULL, NULL);
    proc_memblock_remove_locked(proc, r->_mp, NULL, NULL);
  }
  if (r->__sf) proc_memblock_remove_locked(proc, r->__sf, NULL, NULL);
  if (r->_emergency) proc_memblock_remove_locked(proc, r->_emergency, NULL, NULL);
  if (r->_cvtbuf) proc_memblock_remove_locked(proc, r->_cvtbuf, NULL, NULL);
  if (r->_r48) proc_memblock_remove_locked(proc, r->_r48, NULL, NULL);
  if (r->_localtime_buf) proc_memblock_remove_locked(proc, r->_localtime_buf, NULL, NULL);
  if (r->_asctime_buf) proc_memblock_remove_locked(proc, r->_asctime_buf, NULL, NULL);
  if (r->_misc) proc_memblock_remove_locked(proc, r->_misc, NULL, NULL);
  if (r->_signal_buf) proc_memblock_remove_locked(proc, r->_signal_buf, NULL, NULL);
#else
  if (r->_freelist) {
    for (int i = 0; i <= _Kmax; i++) {
      struct _Bigint *b = r->_freelist[i];
      while (b) {
        proc_memblock_remove_locked(proc, b, NULL, NULL);
        b = b->_next;
      }
    }
    proc_memblock_remove_locked(proc, r->_freelist, NULL, NULL);
  }
  if (r->_result) proc_memblock_remove_locked(proc, r->_result, NULL, NULL);
  if (r->_p5s) proc_memblock_remove_locked(proc, r->_p5s, NULL, NULL);
  if (r->_cvtbuf) proc_memblock_remove_locked(proc, r->_cvtbuf, NULL, NULL);
  if (r->_sig_func) proc_memblock_remove_locked(proc, r->_sig_func, NULL, NULL);
#endif
  extern struct _glue __sglue;
  struct _glue *g = __sglue._next;
  while (g) {
    proc_memblock_remove_locked(proc, g, NULL, NULL);
    g = g->_next;
  }
  pthread_mutex_unlock(&proc->mem_mutex);
#else
  (void)proc;
  (void)r;
#endif
}

void *eos_proc_wrap_pthread(void *data) {
  eos_pwrap_t *pwrap = (eos_pwrap_t *)data;
  if (!pwrap) return NULL;

  eos_proc_t *proc = pwrap->proc;
  void *(*thread_start)(void *) = pwrap->thread_start;
  void *thread_arg = pwrap->thread_arg;
  bool is_main = pwrap->is_main;

  TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();

  if (proc) {
    if (is_main) {
      proc_lock();
      proc->main_thread = cur_task;
      proc_unlock();
    }
    eos_proc_add_thread(proc, cur_task);
    eos_proc_task_set_proc(cur_task, proc);
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  free(pwrap);
  eos_proc_heap_tracking_restore(prev_h);

  void *result = thread_start(thread_arg);

  fflush(stdout);
  fflush(stderr);

  if (proc) {
    eos_proc_remove_thread(proc, cur_task);
    if (is_main) {
#if CONFIG_LIBC_NEWLIB
      extern void esp_reent_cleanup(void);
      esp_reent_cleanup();
#endif
      proc_disown_reent_blocks(proc, __getreent());
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
    eos_proc_task_set_proc(cur_task, proc);
  }

  if (task_func) {
    task_func(task_arg);
  }

  if (proc) {
    proc_disown_reent_blocks(proc, __getreent());
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
