#include "ecore/bin.h"
#include "ecore/dev.h"
#include "ecore/elf.h"
#include "ecore/ioctl.h"
#include "ecore/proc.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int eos_resolve_path(const char *name, char *out, size_t out_len) {
  if (!name || !out || out_len == 0) return -1;

  if (name[0] == '/' || strchr(name, '/')) {
    strlcpy(out, name, out_len);
    struct stat st;
    return (stat(name, &st) == 0) ? 0 : -1;
  }

  const char *path_env = getenv("PATH");
  if (!path_env || !path_env[0]) {
    path_env = "/bin";
  }

  char path_buf[EOS_MID_STR_LEN];
  strlcpy(path_buf, path_env, sizeof(path_buf));

  char *saveptr = NULL;
  char *dir = strtok_r(path_buf, ":", &saveptr);
  char first_dir[EOS_MID_STR_LEN] = "";
  if (dir) {
    strlcpy(first_dir, dir, sizeof(first_dir));
  }

  while (dir) {
    char candidate[EOS_MID_STR_LEN];
    snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
    struct stat st;
    if (stat(candidate, &st) == 0) {
      strlcpy(out, candidate, out_len);
      return 0;
    }
    dir = strtok_r(NULL, ":", &saveptr);
  }

  if (first_dir[0]) {
    snprintf(out, out_len, "%s/%s", first_dir, name);
  } else {
    snprintf(out, out_len, "/bin/%s", name);
  }
  return -1;
}

typedef struct {
  const eos_bin_t *bin;
  eos_bin_t elf_manifest;
  int argc;
  char **argv;
  pid_t pid;
  pid_t ppid;
  pid_t pgid;
  pid_t sid;
} exec_arg_t;

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
  extern void esp_reent_cleanup(void);
  esp_reent_cleanup();

  if (ex) {
    bool prev_h = eos_proc_heap_tracking_disable();
    for (int i = 0; i < ex->argc; i++) {
      free(ex->argv[i]);
    }
    free(ex->argv);
    free(ex);
    eos_proc_heap_tracking_restore(prev_h);
  }

  return (void *)(intptr_t)ret;
}

static char **duplicate_argv(char *const argv[], int *out_argc) {
  int argc = 0;
  while (argv && argv[argc]) {
    argc++;
  }
  *out_argc = argc;

  bool prev_h = eos_proc_heap_tracking_disable();
  char **dup_argv = eos_psram_calloc_prefer(argc + 1, sizeof(char *));
  if (!dup_argv) {
    eos_proc_heap_tracking_restore(prev_h);
    errno = ENOMEM;
    return NULL;
  }

  for (int i = 0; i < argc; i++) {
    dup_argv[i] = eos_psram_strdup(argv[i]);
    if (!dup_argv[i]) {
      for (int j = 0; j < i; j++)
        free(dup_argv[j]);
      free(dup_argv);
      eos_proc_heap_tracking_restore(prev_h);
      errno = ENOMEM;
      return NULL;
    }
  }
  dup_argv[argc] = NULL;
  eos_proc_heap_tracking_restore(prev_h);
  return dup_argv;
}

static void apply_envp(pid_t child_pid, char *const envp[]) {
  if (!envp) return;
  for (int i = 0; envp[i] != NULL; i++) {
    const char *eq = strchr(envp[i], '=');
    if (eq) {
      size_t klen = (size_t)(eq - envp[i]);
      char key[64];
      if (klen < sizeof(key)) {
        memcpy(key, envp[i], klen);
        key[klen] = '\0';
        eos_proc_setenv_pid(child_pid, key, eq + 1, 1);
      }
    }
  }
}

int execve(const char *path, char *const argv[], char *const envp[]) {
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
  char **dup_argv = duplicate_argv(argv, &argc);
  if (!dup_argv) return -1;

  bool prev_h = eos_proc_heap_tracking_disable();
  exec_arg_t *ex = eos_psram_malloc_prefer(sizeof(exec_arg_t));
  eos_proc_heap_tracking_restore(prev_h);
  if (!ex) {
    for (int i = 0; i < argc; i++) free(dup_argv[i]);
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

  eos_proc_t *cur = eos_proc_current();
  pid_t parent_pid = (cur && cur->pid > 0) ? cur->pid : 1;
  pid_t parent_pgid = (cur && cur->pgid > 0) ? cur->pgid : 1;
  pid_t parent_sid = (cur && cur->sid > 0) ? cur->sid : 1;
  pid_t child_pid = eos_proc_create(path, NULL, parent_pid, parent_pgid);
  if (child_pid < 0) {
    for (int i = 0; i < argc; i++) free(dup_argv[i]);
    free(dup_argv);
    free(ex);
    errno = EAGAIN;
    return -1;
  }

  ex->argc = argc;
  ex->argv = dup_argv;
  ex->pid = child_pid;
  ex->ppid = parent_pid;
  ex->pgid = parent_pgid;
  ex->sid = parent_sid;

  apply_envp(child_pid, envp);
  eos_proc_resolve_display_env(child_pid, bin ? bin->flags : EOS_BIN_FLAG_NONE);

  pthread_t thread;
  int rc = eos_proc_spawn_thread(child_pid, exec_thread_entry, ex,
                                 eos_bin_effective_stack_size(bin),
                                 eos_bin_prefers_psram_stack(bin),
                                 &thread);
  if (rc != 0) {
    eos_proc_exit(child_pid, -1);
    int dummy = 0;
    waitpid(child_pid, &dummy, WNOHANG);
    for (int i = 0; i < argc; i++) free(dup_argv[i]);
    free(dup_argv);
    free(ex);
    errno = ENOMEM;
    return -1;
  }

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
  int raw_status = 0;
  pid_t pid = waitpid(-1, &raw_status, 0);
  if (pid < 0) {
    return -1;
  }
  if (status) {
    *status = WIFEXITED(raw_status) ? WEXITSTATUS(raw_status) : raw_status;
  }
  return 0;
}
