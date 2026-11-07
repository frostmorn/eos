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

struct eos_proc_t {
  // Identification & Hierarchy
  pid_t pid;
  pid_t ppid;
  pid_t pgid;
  pid_t sid;
  eos_proc_state_t state;
  int exit_code;
  uint64_t start_time;
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
  int fd_map[FD_SETSIZE];

  // Memory block allocations (Internal SRAM kvec_t for low-latency allocation tracking)
  kvec_t(void *) memblocks;
  pthread_mutex_t mem_mutex;

  // Thread membership & Synchronization
  TaskHandle_t main_thread;
  kvec_psram_t(TaskHandle_t) threads;
  pthread_cond_t wait_cond;
  int last_exit_status;
  bool in_use;
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

// Memory block allocation tracking
void eos_proc_reg_memblock(void *block);
void eos_proc_unreg_memblock(void *block);

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
void eos_proc_add_thread(eos_proc_t *proc, TaskHandle_t thread);
void eos_proc_remove_thread(eos_proc_t *proc, TaskHandle_t thread);

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
