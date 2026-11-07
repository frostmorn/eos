#include "ecore/proc.h"
#include "ecore/bin.h"
#include "ecore/log.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
static pthread_cond_t s_proc_table_cond = PTHREAD_COND_INITIALIZER;
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

extern void eos_log_sync_env(const char *name, const char *value);

static void eos_proc_rebuild_env_array(eos_proc_t *proc) {
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
}

static int eos_proc_setenv_internal(eos_proc_t *proc, const char *name, const char *value, int overwrite) {
  if (!proc || !name || !value) return -1;

  extern void *__real_heap_caps_aligned_alloc_base(size_t alignment, size_t size, uint32_t caps);
  extern void __real_heap_caps_free(void *ptr);

  size_t name_len = strlen(name);
  for (size_t i = 0; i < kv_size(proc->env); i++) {
    char *entry = kv_A(proc->env, i);
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      if (!overwrite) return 0;
      size_t new_len = name_len + 1 + strlen(value) + 1;
      char *new_entry = __real_heap_caps_aligned_alloc_base(4, new_len, MALLOC_CAP_DEFAULT);
      if (!new_entry) return -1;
      snprintf(new_entry, new_len, "%s=%s", name, value);
      __real_heap_caps_free(entry);
      kv_A(proc->env, i) = new_entry;
      eos_proc_rebuild_env_array(proc);
      return 0;
    }
  }

  size_t new_len = name_len + 1 + strlen(value) + 1;
  char *new_entry = __real_heap_caps_aligned_alloc_base(4, new_len, MALLOC_CAP_DEFAULT);
  if (!new_entry) return -1;
  snprintf(new_entry, new_len, "%s=%s", name, value);
  kv_push(char *, proc->env, new_entry);
  eos_proc_rebuild_env_array(proc);
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

eos_proc_t *eos_proc_current(void) {
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || xPortInIsrContext()) {
    return NULL;
  }

  TaskHandle_t task = xTaskGetCurrentTaskHandle();
  if (task == NULL) {
    return NULL;
  }

  const char *name = pcTaskGetName(NULL);
  if (name && (strncmp(name, "ipc", 3) == 0 ||
               strncmp(name, "esp_", 4) == 0 ||
               strncmp(name, "IDLE", 4) == 0 ||
               strcmp(name, "tiT") == 0 ||
               strcmp(name, "sys_evt") == 0 ||
               strcmp(name, "wifi") == 0)) {
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
  if (!s_proc_mutex_inited) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_proc_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    s_proc_mutex_inited = true;
  }

  proc_lock();
  if (s_proc_table != NULL) {
    proc_unlock();
    return;
  }

  s_proc_table = (eos_proc_t *)eos_psram_calloc_prefer(EOS_MAX_PROCS, sizeof(eos_proc_t));
  if (!s_proc_table) {
    s_proc_table = (eos_proc_t *)calloc(EOS_MAX_PROCS, sizeof(eos_proc_t));
  }
  if (!s_proc_table) {
    EOS_LOGE("Failed to allocate process table!\n");
    proc_unlock();
    return;
  }

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
  pthread_mutexattr_t m_attr;
  pthread_mutexattr_init(&m_attr);
  pthread_mutexattr_settype(&m_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&init_proc->mem_mutex, &m_attr);
  pthread_mutexattr_destroy(&m_attr);

  for (int i = 0; i < FD_SETSIZE; i++) {
    init_proc->fd_map[i] = -1;
  }

  pthread_cond_init(&init_proc->wait_cond, NULL);
  eos_proc_init_env(init_proc);

  init_proc->main_thread = xTaskGetCurrentTaskHandle();
  kv_push(TaskHandle_t, init_proc->threads, init_proc->main_thread);

  proc_unlock();
  EOS_LOGI("Process table initialized (max %u procs in PSRAM)\n", (unsigned)EOS_MAX_PROCS);
}

pid_t eos_proc_create(const char *name, const char *cmdline, pid_t ppid, pid_t pgid) {
  if (!s_proc_table) {
    eos_proc_init();
  }

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
    EOS_LOGE("Process table full (all %u slots in use)\n", (unsigned)EOS_MAX_PROCS);
    errno = EAGAIN;
    return -1;
  }

  pid_t new_pid = alloc_next_pid_locked();
  if (new_pid < 0) {
    proc_unlock();
    errno = EAGAIN;
    return -1;
  }

  eos_proc_t *cur = eos_proc_current();
  eos_proc_t *p = &s_proc_table[slot];
  memset(p, 0, sizeof(eos_proc_t));

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
  pthread_cond_init(&p->wait_cond, NULL);

  kv_init(p->fds);
  kv_init(p->dirs);
  kv_init(p->memblocks);
  kv_init(p->env);
  kv_init(p->threads);
  pthread_mutexattr_t m_attr;
  pthread_mutexattr_init(&m_attr);
  pthread_mutexattr_settype(&m_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&p->mem_mutex, &m_attr);
  pthread_mutexattr_destroy(&m_attr);

  for (int i = 0; i < FD_SETSIZE; i++) {
    p->fd_map[i] = -1;
  }

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
  return new_pid;
}

static void eos_proc_add_thread_locked(eos_proc_t *proc, TaskHandle_t thread) {
  if (!proc || !thread) return;
  for (size_t i = 0; i < kv_size(proc->threads); i++) {
    if (kv_A(proc->threads, i) == thread) return;
  }
  kv_push(TaskHandle_t, proc->threads, thread);
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
  if (!s_proc_table) return NULL;
  for (size_t i = 0; i < EOS_MAX_PROCS; i++) {
    if (s_proc_table[i].in_use && s_proc_table[i].pid == pid) {
      return &s_proc_table[i];
    }
  }
  return NULL;
}

bool eos_proc_get_copy(pid_t pid, eos_proc_t *out_proc) {
  if (!s_proc_table || !out_proc) return false;
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
  if (!s_proc_table || !out_pids || max_pids == 0) return 0;
  proc_lock();
  size_t count = 0;
  for (size_t i = 0; i < EOS_MAX_PROCS && count < max_pids; i++) {
    if (s_proc_table[i].in_use) {
      out_pids[count++] = s_proc_table[i].pid;
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
      extern void __real_heap_caps_free(void *ptr);

      for (size_t j = 0; j < kv_size(z->fds); j++) {
        close(kv_A(z->fds, j));
      }
      kv_destroy(z->fds);

      for (size_t j = 0; j < kv_size(z->dirs); j++) {
        closedir(kv_A(z->dirs, j));
      }
      kv_destroy(z->dirs);

      for (size_t j = 0; j < kv_size(z->memblocks); j++) {
        void *b = kv_A(z->memblocks, j);
        if (b) __real_heap_caps_free(b);
      }
      kv_destroy(z->memblocks);

      for (size_t j = 0; j < kv_size(z->env); j++) {
        char *e = kv_A(z->env, j);
        if (e) __real_heap_caps_free(e);
      }
      kv_destroy(z->env);

      if (z->env_array) {
        __real_heap_caps_free(z->env_array);
        z->env_array = NULL;
      }

      if (z->cmdline) {
        eos_psram_free(z->cmdline);
        z->cmdline = NULL;
      }

      kv_destroy(z->threads);
      pthread_cond_destroy(&z->wait_cond);
      pthread_mutex_destroy(&z->mem_mutex);
      memset(z, 0, sizeof(eos_proc_t));
      z->in_use = false;
      z->state = EOS_PROC_STATE_UNUSED;

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

// ── Memory Block Allocation Tracking ──────────────────────────

void eos_proc_reg_memblock(void *block) {
  if (!block) return;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    pthread_mutex_lock(&proc->mem_mutex);
    kv_push(void *, proc->memblocks, block);
    pthread_mutex_unlock(&proc->mem_mutex);
  }
}

void eos_proc_unreg_memblock(void *block) {
  if (!block) return;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    pthread_mutex_lock(&proc->mem_mutex);
    for (size_t i = 0; i < kv_size(proc->memblocks); i++) {
      if (kv_A(proc->memblocks, i) == block) {
        kv_drop_fast(void *, proc->memblocks, i);
        break;
      }
    }
    pthread_mutex_unlock(&proc->mem_mutex);
  }
}

// ── File Descriptor & Directory Tracking ──────────────────────

int eos_proc_resolve_fd(int fd) {
  if (fd < 0 || fd >= FD_SETSIZE) return fd;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return fd;
  int mapped = proc->fd_map[fd];
  return (mapped >= 0) ? mapped : fd;
}

void eos_proc_reg_fd(int fd) {
  if (fd < 0) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;
  kv_push(int, proc->fds, fd);
}

void eos_proc_unreg_fd(int fd) {
  if (fd < 0) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;

  for (size_t i = 0; i < kv_size(proc->fds); i++) {
    if (kv_A(proc->fds, i) == fd) {
      kv_drop_fast(int, proc->fds, i);
      kv_opt(int, proc->fds);
      break;
    }
  }
}

void eos_proc_reg_dir(DIR *dir) {
  if (!dir) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;
  kv_push(DIR *, proc->dirs, dir);
}

void eos_proc_unreg_dir(DIR *dir) {
  if (!dir) return;
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return;

  for (size_t i = 0; i < kv_size(proc->dirs); i++) {
    if (kv_A(proc->dirs, i) == dir) {
      kv_drop_fast(DIR *, proc->dirs, i);
      kv_opt(DIR *, proc->dirs);
      break;
    }
  }
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
    eos_proc_add_thread(proc, cur_task);
  }

  void *result = thread_start(thread_arg);

  fflush(stdout);
  fflush(stderr);

  if (proc) {
    eos_proc_remove_thread(proc, cur_task);
    if (is_main) {
      eos_proc_exit(proc->pid, (int)(intptr_t)result);
    }
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
    eos_proc_remove_thread(proc, cur_task);
    if (is_main) {
      eos_proc_exit(proc->pid, 0);
    }
  }

  vTaskDelete(NULL);
}

size_t eos_thread_stack_high_water_mark(void) {
  return (size_t)uxTaskGetStackHighWaterMark(NULL);
}
