#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
//
// TODO: This part is a redesign for existing application context
//
///////////////////////////////////////////////////////

#include "emisc/kvec.h"
#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <sys/select.h>

typedef struct eos_tctx_t eos_tctx_t;
struct eos_tctx_t {
  // Current working directory
  char cwd[PATH_MAX];
  // File descriptors in use
  kvec_t(int) fds;
  // Opened directories
  kvec_t(DIR *) dirs;
  // Allocated memory blocks
  kvec_t(void *) memblocks;
  // Per-thread virtual descriptor mapping (index = virtual fd, value = real VFS fd or -1)
  int fd_map[FD_SETSIZE];
  // Environment variables ("KEY=VALUE")
  kvec_t(char *) env;
  // Environment pointer array for environ macro
  char **env_array;
  // Last execution exit status ($?)
  int last_exit_status;
};

// Retrieves current thread context
eos_tctx_t *eos_tctx_get();

// Environment management
void eos_tctx_init_env(eos_tctx_t *tctx);
char *eos_tctx_getenv(const char *name);
int eos_tctx_setenv(const char *name, const char *value, int overwrite);
int eos_tctx_unsetenv(const char *name);
char **eos_tctx_get_environ(void);

// FD management
void eos_tctx_reg_fd(int fd, eos_tctx_t *tctx);
void eos_tctx_unreg_fd(int fd, eos_tctx_t *tctx);

// FD redirection / mapping
int eos_tctx_resolve_fd(int fd);

// DIR management
void eos_tctx_reg_dir(DIR *dir, eos_tctx_t *tctx);
void eos_tctx_unreg_dir(DIR *dir, eos_tctx_t *tctx);

// Memory management
void eos_tctx_reg_memblock(void *block, eos_tctx_t *tctx);
void eos_tctx_unreg_memblock(void *block, eos_tctx_t *tctx);

/// => thread wrap

typedef struct {
  void *(*thread_start)(void *);
  void *thread_arg;
  char cwd[PATH_MAX];
  int stdio_fds[3];
} eos_twrap_t;

// Preapare data for thread wrap
// To simplify casting, we it's as a void *, since it doesn't really matter
eos_twrap_t *eos_twrap_prepare(void *thread_start, void *thread_data);

// Wrapper function arround pthread and freertos task routine
// to be passed from thread create wrap
void *eos_twrap_pthread(void *data);
// yeah, rtos have a bit different TaskEntry declaration
void eos_twrap_freertos(void *data);
