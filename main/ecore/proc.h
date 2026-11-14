#pragma once

#include "emisc/kvec.h"
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef EOS_MAX_PROCS
#define EOS_MAX_PROCS 128
#endif

typedef enum {
  EOS_PROC_STATE_UNUSED = 0,
  EOS_PROC_STATE_RUNNING = 'R',  // Executing or ready
  EOS_PROC_STATE_SLEEPING = 'S', // Blocked / waiting
  EOS_PROC_STATE_STOPPED = 'T',  // Suspended (SIGTSTP)
  EOS_PROC_STATE_ZOMBIE = 'Z',   // Terminated, waiting for waitpid()
} eos_proc_state_t;

typedef struct eos_proc_t eos_proc_t;

// Process memory tracking entry
typedef struct {
  void *ptr;
  size_t size;
  bool is_psram;
} eos_memblock_t;

struct eos_proc_t {
  // Identification & Hierarchy
  pid_t pid;
  pid_t ppid;
  pid_t pgid;
  pid_t sid;
  uint32_t state : 8;       // eos_proc_state_t
  uint32_t in_use : 1;
  uint32_t reserved_flags : 23;
  int exit_code;
  uint64_t start_time;
  uint64_t cpu_time_us;
  char name[32];
  char *cmdline;
  char tty[16];

  // Working Directory
  char cwd[PATH_MAX];

  // Environment variables (PSRAM-backed)
  kvec_psram_t(char *) env;
  char **env_array;

  // File descriptors in use (PSRAM-backed) & mapping
  kvec_psram_t(int) fds;
  kvec_psram_t(DIR *) dirs;
  kvec_psram_t(int) fd_map;
  pthread_mutex_t fd_mutex;

  // Memory block allocations (Internal SRAM kvec_t for low-latency allocation tracking)
  kvec_t(eos_memblock_t) memblocks;
  pthread_mutex_t mem_mutex;
  size_t heap_sram;
  size_t heap_psram;

  // Thread membership & Synchronization
  TaskHandle_t main_thread;
  kvec_psram_t(TaskHandle_t) threads;
  pthread_cond_t wait_cond;
  int last_exit_status;
};

// Process subsystem initialization (called from app_main)
void eos_proc_init(void);

// Retrieves current process context for the calling thread
eos_proc_t *eos_proc_current(void);

// Process lifecycle operations
pid_t eos_proc_create(const char *name, const char *cmdline, pid_t ppid, pid_t pgid);
void eos_proc_set_thread(pid_t pid, TaskHandle_t thread);
void eos_proc_set_state(pid_t pid, eos_proc_state_t state);
void eos_proc_set_tty(pid_t pid, const char *tty_name);
void eos_proc_set_cwd(pid_t pid, const char *cwd);
void eos_proc_exit(pid_t pid, int exit_code);

// Process queries & inspection
eos_proc_t *eos_proc_find(pid_t pid);
bool eos_proc_get_copy(pid_t pid, eos_proc_t *out_proc);
size_t eos_proc_get_all_pids(pid_t *out_pids, size_t max_pids);
pid_t eos_proc_get_pgid(pid_t pid);
int eos_proc_set_pgid(pid_t pid, pid_t pgid);
pid_t eos_proc_setsid(pid_t pid);
size_t eos_proc_count_active(void);
size_t eos_proc_snapshot(eos_proc_t *out_table, size_t max_count);

// POSIX API implementations
pid_t eos_getpid(void);
pid_t eos_getppid(void);
pid_t eos_getpgid(pid_t pid);
int eos_setpgid(pid_t pid, pid_t pgid);
pid_t eos_setsid(void);
pid_t eos_waitpid(pid_t pid, int *status, int options);

pid_t getppid(void);
pid_t getpgid(pid_t pid);
int setpgid(pid_t pid, pid_t pgid);
pid_t setsid(void);
pid_t waitpid(pid_t pid, int *status, int options);

// Environment management
void eos_proc_init_env(eos_proc_t *proc);
char *eos_proc_getenv(const char *name);
int eos_proc_setenv(const char *name, const char *value, int overwrite);
int eos_proc_unsetenv(const char *name);
char **eos_proc_get_environ(void);

// Memory block allocation tracking & registry
void eos_proc_reg_memblock(void *block);
void eos_proc_unreg_memblock(void *block);
void eos_proc_mem_insert(eos_proc_t *proc, void *ptr, size_t size, bool is_psram);
void eos_proc_mem_realloc(eos_proc_t *proc, void *old_ptr, void *new_ptr, size_t new_size, bool is_psram);
void eos_proc_mem_remove(eos_proc_t *proc, void *ptr);
int eos_mem_adopt(void *ptr, pid_t target_pid);

// Task flags and classification

bool eos_proc_task_is_lwip(void);
bool eos_proc_task_is_system(void);
void eos_proc_task_classify(TaskHandle_t task, const char *name);

// Kernel execution context
bool eos_proc_enter_kernel(void);
void eos_proc_leave_kernel(bool prev);
bool eos_proc_is_kernel(void);

// Process heap tracking toggle helpers (controls tracking of internal metadata)
bool eos_proc_heap_tracking_disable(void);
void eos_proc_heap_tracking_restore(bool prev);
bool eos_proc_heap_tracking_is_disabled(void);
bool eos_proc_in_heap_wrap(void);
void eos_proc_set_in_heap_wrap(bool val);
void eos_proc_task_flags_cleanup(void *task);

// File descriptor & directory tracking
void eos_proc_reg_fd(int fd);
void eos_proc_unreg_fd(int fd);
int eos_proc_resolve_fd(int fd);
void eos_proc_reg_dir(DIR *dir);
void eos_proc_unreg_dir(DIR *dir);

// FD snapshot & leak cleanup
typedef struct {
  int *fds;
  size_t count;
  DIR **dirs;
  size_t dir_count;
  bool valid;
} eos_proc_fd_snapshot_t;

eos_proc_fd_snapshot_t eos_proc_snapshot_fds(void);
void eos_proc_cleanup_leaked_fds(eos_proc_fd_snapshot_t *snap);

// Thread management & wrapping
int eos_proc_spawn_thread(pid_t pid, void *(*start_routine)(void *), void *arg,
                          uint32_t stack_size, bool prefer_psram, pthread_t *thread_out);
void eos_proc_add_thread(eos_proc_t *proc, TaskHandle_t thread);
void eos_proc_remove_thread(eos_proc_t *proc, TaskHandle_t thread);
uint64_t eos_proc_get_cpu_time_us(eos_proc_t *proc);

typedef struct {
  void *(*thread_start)(void *);
  void *thread_arg;
  eos_proc_t *proc;
  bool is_main;
} eos_pwrap_t;

eos_pwrap_t *eos_proc_wrap_prepare(void *thread_start, void *thread_data);
void *eos_proc_wrap_pthread(void *data);
void eos_proc_wrap_freertos(void *data);
size_t eos_thread_stack_high_water_mark(void);

// PSRAM kvec FD mapping helpers
static inline int eos_proc_fd_map_get(const eos_proc_t *proc, int fd) {
  if (!proc || fd < 0)
    return -1;
  eos_proc_t *p = (eos_proc_t *)proc;
  pthread_mutex_lock(&p->fd_mutex);
  int res = -1;
  if ((size_t)fd < kv_size(p->fd_map)) {
    res = kv_A(p->fd_map, fd);
  }
  pthread_mutex_unlock(&p->fd_mutex);
  return res;
}

static inline void eos_proc_fd_map_set(eos_proc_t *proc, int fd, int real_fd) {
  if (!proc || fd < 0) return;
  pthread_mutex_lock(&proc->fd_mutex);
  bool prev_h = eos_proc_heap_tracking_disable();
  while (kv_size(proc->fd_map) <= (size_t)fd) {
    kv_push(int, proc->fd_map, -1);
  }
  kv_A(proc->fd_map, fd) = real_fd;
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&proc->fd_mutex);
}

static inline void eos_proc_fd_map_reset(eos_proc_t *proc) {
  if (!proc) return;
  pthread_mutex_lock(&proc->fd_mutex);
  for (size_t i = 0; i < kv_size(proc->fd_map); i++) {
    kv_A(proc->fd_map, i) = -1;
  }
  pthread_mutex_unlock(&proc->fd_mutex);
}
