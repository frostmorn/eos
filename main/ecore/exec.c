#include "ecore/bin.h"
#include "ecore/elf.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const eos_bin_t *bin;
  eos_bin_t elf_manifest;
  int argc;
  char **argv;
} exec_arg_t;

// Track last spawned thread for eos_wait()
static pthread_t last_child_thread = 0;
static bool has_child_thread = false;
static pthread_mutex_t exec_mutex = PTHREAD_MUTEX_INITIALIZER;

static const eos_bin_t *eos_bin_find_by_name(const char *name) {
  if (!name)
    return NULL;

  // Strip path prefix if present
  const char *p = strrchr(name, '/');
  if (p)
    name = p + 1;

  for (const eos_bin_t *bin = _eos_bins_start; bin < _eos_bins_end; bin++) {
    if (strcmp(bin->filename, name) == 0)
      return bin;
  }
  return NULL;
}

static void *exec_thread_entry(void *arg) {
  exec_arg_t *ex = (exec_arg_t *)arg;
  int ret = -1;

  if (ex && ex->bin && ex->bin->entry_point) {
    ret = ex->bin->entry_point(ex->argc, ex->argv);
  }

  fflush(stdout);
  fflush(stderr);

  if (ex) {
    for (int i = 0; i < ex->argc; i++) {
      free(ex->argv[i]);
    }
    free(ex->argv);
    free(ex);
  }

  return (void *)(intptr_t)ret;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
  (void)envp;
  if (!path) {
    errno = EINVAL;
    return -1;
  }

  const eos_bin_t *bin = eos_bin_find_by_name(path);
  bool is_elf = false;
  if (!bin) {
    if (eos_is_elf_file(path)) {
      is_elf = true;
    } else {
      EOS_LOGE("execve: binary not found: %s", path);
      errno = ENOENT;
      return -1;
    }
  }

  int argc = 0;
  while (argv && argv[argc]) {
    argc++;
  }

  char **dup_argv = eos_psram_calloc_prefer(argc + 1, sizeof(char *));
  if (!dup_argv) {
    errno = ENOMEM;
    return -1;
  }

  for (int i = 0; i < argc; i++) {
    dup_argv[i] = eos_psram_strdup(argv[i]);
    if (!dup_argv[i]) {
      for (int j = 0; j < i; j++)
        free(dup_argv[j]);
      free(dup_argv);
      errno = ENOMEM;
      return -1;
    }
  }
  dup_argv[argc] = NULL;

  exec_arg_t *ex = eos_psram_malloc_prefer(sizeof(exec_arg_t));
  if (!ex) {
    for (int i = 0; i < argc; i++)
      free(dup_argv[i]);
    free(dup_argv);
    errno = ENOMEM;
    return -1;
  }

  if (is_elf) {
    memset(&ex->elf_manifest, 0, sizeof(ex->elf_manifest));
    ex->elf_manifest.magic = EOS_BIN_MAGIC;
    ex->elf_manifest.flags = EOS_BIN_FLAG_STACK_SRAM;
    ex->elf_manifest.stack_size = EOS_ELF_DEFAULT_STACK_SIZE;
    strlcpy(ex->elf_manifest.filename, path, sizeof(ex->elf_manifest.filename));
    strlcpy(ex->elf_manifest.name, path, sizeof(ex->elf_manifest.name));
    ex->elf_manifest.entry_point = eos_elf_stage_entry;
    ex->bin = &ex->elf_manifest;
    bin = &ex->elf_manifest;
  } else {
    ex->bin = bin;
  }

  ex->argc = argc;
  ex->argv = dup_argv;

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr, eos_bin_effective_stack_size(bin));

  eos_pthread_set_manifest_cfg(bin);

  // Create joinable thread using stack_size and preference from the ebin manifest
  int rc = pthread_create(&thread, &attr, exec_thread_entry, ex);
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    for (int i = 0; i < argc; i++)
      free(dup_argv[i]);
    free(dup_argv);
    free(ex);
    errno = ENOMEM;
    return -1;
  }

  pthread_mutex_lock(&exec_mutex);
  last_child_thread = thread;
  has_child_thread = true;
  pthread_mutex_unlock(&exec_mutex);

  return 0;
}

int execv(const char *path, char *const argv[]) {
  return execve(path, argv, NULL);
}

int execl(const char *path, const char *arg, ...) {
  va_list args;
  va_start(args, arg);
  int argc = 1;
  while (va_arg(args, const char *) != NULL) {
    argc++;
  }
  va_end(args);

  char **argv = eos_psram_calloc_prefer(argc + 1, sizeof(char *));
  if (!argv) {
    errno = ENOMEM;
    return -1;
  }

  argv[0] = (char *)arg;
  va_start(args, arg);
  for (int i = 1; i < argc; i++) {
    argv[i] = (char *)va_arg(args, const char *);
  }
  argv[argc] = NULL;
  va_end(args);

  int ret = execv(path, argv);
  free(argv);
  return ret;
}

int execle(const char *path, const char *arg, ...) {
  return execv(path, (char *const *)&arg);
}

int execlp(const char *file, const char *arg, ...) {
  return execv(file, (char *const *)&arg);
}

int execvp(const char *file, char *const argv[]) {
  return execv(file, argv);
}

int eos_wait(int *status) {
  pthread_t thread_to_join = 0;
  pthread_mutex_lock(&exec_mutex);
  if (!has_child_thread) {
    pthread_mutex_unlock(&exec_mutex);
    errno = ECHILD;
    return -1;
  }
  thread_to_join = last_child_thread;
  has_child_thread = false;
  pthread_mutex_unlock(&exec_mutex);

  void *retval = NULL;
  int rc = pthread_join(thread_to_join, &retval);
  if (rc != 0) {
    errno = EINVAL;
    return -1;
  }

  if (status) {
    *status = (int)(intptr_t)retval;
  }
  return 0;
}
