#include "ecore/binfs.h"
#include "ecore/pipefs.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// This call isn't implemented in picolib, but cause it have a declaration
// burried somewhere in <stdlib.h> why not to use it, right?
// components/esp_lib/src/syscalls.c
extern int __real_system(const char *str);

#define EOS_SYSTEM_MAX_STAGES 8
#define EOS_SYSTEM_MAX_ARGS   32

typedef struct {
  int argc;
  char *argv[EOS_SYSTEM_MAX_ARGS];
  char *in_file;
  char *out_file;
  char *err_file;
  bool out_append;
  bool err_append;
  bool err_to_out; // 2>&1
  bool out_to_err; // 1>&2
} eos_cmd_stage_t;

typedef struct {
  eos_bin_t manifest;
  int argc;
  char **argv;
  int pipe_in_fd;
  int pipe_out_fd;
  char *in_file;
  char *out_file;
  char *err_file;
  bool out_append;
  bool err_append;
  bool err_to_out;
  bool out_to_err;
} eos_stage_exec_t;

static void resolve_path(const char *name, char *out, size_t len) {
  if (name[0] == '/') {
    strlcpy(out, name, len);
  } else {
    snprintf(out, len, EOS_BINFS_ROOT "/%s", name);
  }
}

// Reads the next shell token from *cursor, handling single/double quotes
// and operator tokens (|, <, >, >>, 2>, 2>>, 2>&1, 1>&2, &>, &>>).
// Sets *is_op = true when an unquoted operator token is returned.
static char *next_shell_token(char **cursor, char **arena, bool *is_op) {
  char *p = *cursor;
  while (*p && isspace((unsigned char)*p))
    p++;

  if (!*p) {
    *cursor = p;
    return NULL;
  }

  *is_op = false;

  // Check multi-char and single-char redirection / pipeline operators
  if (*p == '|') {
    *is_op = true;
    *cursor = p + 1;
    return "|";
  }
  if (strncmp(p, "2>&1", 4) == 0) {
    *is_op = true;
    *cursor = p + 4;
    return "2>&1";
  }
  if (strncmp(p, "1>&2", 4) == 0) {
    *is_op = true;
    *cursor = p + 4;
    return "1>&2";
  }
  if (strncmp(p, "&>>", 3) == 0) {
    *is_op = true;
    *cursor = p + 3;
    return "&>>";
  }
  if (strncmp(p, "&>", 2) == 0) {
    *is_op = true;
    *cursor = p + 2;
    return "&>";
  }
  if (strncmp(p, "2>>", 3) == 0) {
    *is_op = true;
    *cursor = p + 3;
    return "2>>";
  }
  if (strncmp(p, "2>", 2) == 0) {
    *is_op = true;
    *cursor = p + 2;
    return "2>";
  }
  if (strncmp(p, "1>>", 3) == 0) {
    *is_op = true;
    *cursor = p + 3;
    return ">>";
  }
  if (strncmp(p, "1>", 2) == 0) {
    *is_op = true;
    *cursor = p + 2;
    return ">";
  }
  if (strncmp(p, "0<", 2) == 0) {
    *is_op = true;
    *cursor = p + 2;
    return "<";
  }
  if (strncmp(p, ">>", 2) == 0) {
    *is_op = true;
    *cursor = p + 2;
    return ">>";
  }
  if (*p == '>') {
    *is_op = true;
    *cursor = p + 1;
    return ">";
  }
  if (*p == '<') {
    *is_op = true;
    *cursor = p + 1;
    return "<";
  }

  // Regular argument (supports single and double quotes and backslash escapes)
  char *out = *arena;
  char *w = out;

  while (*p && !isspace((unsigned char)*p)) {
    if (*p == '|' || *p == '<' || *p == '>')
      break;
    if ((p[0] == '1' || p[0] == '2') && p[1] == '>')
      break;
    if (p[0] == '&' && p[1] == '>')
      break;

    if (*p == '\'' || *p == '"') {
      char quote = *p++;
      while (*p && *p != quote) {
        if (quote == '"' && *p == '\\' && p[1])
          p++;
        *w++ = *p++;
      }
      if (*p == quote)
        p++;
    } else if (*p == '\\' && p[1]) {
      p++;
      *w++ = *p++;
    } else {
      *w++ = *p++;
    }
  }

  *w++ = '\0';
  *arena = w;
  *cursor = p;
  return out;
}

static int parse_cmdline(const char *cmdline, char *arena_buf,
                         eos_cmd_stage_t *stages, int *stage_count_out) {
  int stage_idx = 0;
  memset(stages, 0, sizeof(eos_cmd_stage_t) * EOS_SYSTEM_MAX_STAGES);

  char *cursor = (char *)cmdline;
  char *arena = arena_buf;
  bool is_op = false;
  char *tok = NULL;

  while ((tok = next_shell_token(&cursor, &arena, &is_op)) != NULL) {
    eos_cmd_stage_t *st = &stages[stage_idx];

    if (is_op) {
      if (strcmp(tok, "|") == 0) {
        if (st->argc == 0) {
          EOS_LOGE("eos_system() syntax error: empty pipe stage");
          errno = EINVAL;
          return -1;
        }
        if (stage_idx + 1 >= EOS_SYSTEM_MAX_STAGES) {
          EOS_LOGE("eos_system() too many pipeline stages");
          errno = E2BIG;
          return -1;
        }
        st->argv[st->argc] = NULL;
        stage_idx++;
        continue;
      }

      if (strcmp(tok, "2>&1") == 0) {
        st->err_to_out = true;
        st->out_to_err = false;
        continue;
      }

      if (strcmp(tok, "1>&2") == 0) {
        st->out_to_err = true;
        st->err_to_out = false;
        continue;
      }

      // Redirection operator requiring a target filename token
      bool target_is_op = false;
      char *target = next_shell_token(&cursor, &arena, &target_is_op);
      if (!target || target_is_op || target[0] == '\0') {
        EOS_LOGE("eos_system() syntax error: missing target for '%s'", tok);
        errno = EINVAL;
        return -1;
      }

      if (strcmp(tok, "<") == 0) {
        st->in_file = target;
      } else if (strcmp(tok, ">") == 0) {
        st->out_file = target;
        st->out_append = false;
      } else if (strcmp(tok, ">>") == 0) {
        st->out_file = target;
        st->out_append = true;
      } else if (strcmp(tok, "2>") == 0) {
        st->err_file = target;
        st->err_append = false;
      } else if (strcmp(tok, "2>>") == 0) {
        st->err_file = target;
        st->err_append = true;
      } else if (strcmp(tok, "&>") == 0) {
        st->out_file = target;
        st->out_append = false;
        st->err_to_out = true;
      } else if (strcmp(tok, "&>>") == 0) {
        st->out_file = target;
        st->out_append = true;
        st->err_to_out = true;
      }
      continue;
    }

    if (st->argc + 1 >= EOS_SYSTEM_MAX_ARGS) {
      EOS_LOGE("eos_system() too many arguments");
      errno = E2BIG;
      return -1;
    }
    st->argv[st->argc++] = tok;
  }

  stages[stage_idx].argv[stages[stage_idx].argc] = NULL;

  if (stage_idx > 0 && stages[stage_idx].argc == 0) {
    EOS_LOGE("eos_system() syntax error: trailing pipe '|'");
    errno = EINVAL;
    return -1;
  }

  *stage_count_out = (stages[0].argc == 0) ? 0 : (stage_idx + 1);
  return 0;
}

static int load_bin_manifest(const char *cmd, eos_bin_t *manifest_out) {
  char path[EOS_MID_STR_LEN];
  resolve_path(cmd, path, sizeof(path));

  FILE *f = fopen(path, "r");
  if (!f) {
    EOS_LOGE("eos_system() not found: %s", path);
    errno = ENOENT;
    return -1;
  }

  uint32_t magic = 0;
  if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != EOS_BIN_MAGIC) {
    fclose(f);
    EOS_LOGE("eos_system() unknown format: %s", path);
    errno = ENOEXEC;
    return -1;
  }

  rewind(f);
  if (fread(manifest_out, sizeof(*manifest_out), 1, f) != 1) {
    fclose(f);
    errno = EIO;
    return -1;
  }
  fclose(f);
  return 0;
}

static int apply_stage_redirections(const eos_stage_exec_t *st,
                                    int opened_fds[3]) {
  opened_fds[0] = -1;
  opened_fds[1] = -1;
  opened_fds[2] = -1;

  // 1. Pipe descriptors first (explicit file redirections override pipes)
  if (st->pipe_in_fd >= 0) {
    dup2(st->pipe_in_fd, STDIN_FILENO);
  }
  if (st->pipe_out_fd >= 0) {
    dup2(st->pipe_out_fd, STDOUT_FILENO);
  }

  // 2. Explicit input redirection (< file)
  if (st->in_file) {
    int fd = open(st->in_file, O_RDONLY, 0);
    if (fd < 0) {
      EOS_LOGE("eos_system() failed to open input '%s': %s", st->in_file,
               strerror(errno));
      return -1;
    }
    opened_fds[0] = fd;
    dup2(fd, STDIN_FILENO);
  }

  // 3. Explicit output redirection (> file or >> file)
  if (st->out_file) {
    int flags = O_WRONLY | O_CREAT | (st->out_append ? O_APPEND : O_TRUNC);
    int fd = open(st->out_file, flags, 0644);
    if (fd < 0) {
      EOS_LOGE("eos_system() failed to open output '%s': %s", st->out_file,
               strerror(errno));
      return -1;
    }
    opened_fds[1] = fd;
    dup2(fd, STDOUT_FILENO);
  }

  // 4. Explicit stderr redirection (2> file or 2>> file)
  if (st->err_file) {
    int flags = O_WRONLY | O_CREAT | (st->err_append ? O_APPEND : O_TRUNC);
    int fd = open(st->err_file, flags, 0644);
    if (fd < 0) {
      EOS_LOGE("eos_system() failed to open error output '%s': %s",
               st->err_file, strerror(errno));
      return -1;
    }
    opened_fds[2] = fd;
    dup2(fd, STDERR_FILENO);
  }

  // 5. Stream duplication (2>&1 or 1>&2)
  if (st->err_to_out) {
    dup2(STDOUT_FILENO, STDERR_FILENO);
  }
  if (st->out_to_err) {
    dup2(STDERR_FILENO, STDOUT_FILENO);
  }

  return 0;
}

static void close_stage_fds(int opened_fds[3], int pipe_in_fd,
                            int pipe_out_fd) {
  fflush(stdout);
  fflush(stderr);
  clearerr(stdin);

  for (int i = 0; i < 3; i++) {
    if (opened_fds[i] >= 0) {
      close(opened_fds[i]);
      opened_fds[i] = -1;
    }
  }
  if (pipe_in_fd >= 0)
    close(pipe_in_fd);
  if (pipe_out_fd >= 0)
    close(pipe_out_fd);
}

static void *pipeline_stage_thread(void *arg) {
  eos_stage_exec_t *st = (eos_stage_exec_t *)arg;
  int opened_fds[3] = {-1, -1, -1};
  int ret = -1;

  // Transfer ownership of the inherited pipe descriptors into this stage's
  // thread context so closing them on stage completion unregisters cleanly.
  eos_tctx_t *tctx = eos_tctx_get();
  if (tctx) {
    if (st->pipe_in_fd >= 0)
      eos_tctx_reg_fd(st->pipe_in_fd, tctx);
    if (st->pipe_out_fd >= 0)
      eos_tctx_reg_fd(st->pipe_out_fd, tctx);
  }

  if (apply_stage_redirections(st, opened_fds) == 0) {
    ret = st->manifest.entry_point(st->argc, st->argv);
  }

  // Clear thread-local stdio mappings before closing underlying fds
  if (tctx) {
    tctx->fd_map[STDIN_FILENO] = -1;
    tctx->fd_map[STDOUT_FILENO] = -1;
    tctx->fd_map[STDERR_FILENO] = -1;
  }

  close_stage_fds(opened_fds, st->pipe_in_fd, st->pipe_out_fd);
  return (void *)(intptr_t)ret;
}

// TODO: make it's behaviour as close as possible to system() from <stdlib.h>
int __wrap_system(const char *cmdline) {
  if (!cmdline) {
    EOS_LOGE("eos_system() called with null cmdline");
    return -1;
  }

  // Allocate arena large enough to hold all unquoted/extracted tokens
  size_t cmd_len = strlen(cmdline);
  char *arena_buf = malloc(cmd_len + EOS_SYSTEM_MAX_ARGS + 16);
  if (!arena_buf) {
    EOS_LOGE("eos_system() %d: %s", errno, strerror(errno));
    return -1;
  }

  eos_cmd_stage_t *stages =
      calloc(EOS_SYSTEM_MAX_STAGES, sizeof(eos_cmd_stage_t));
  if (!stages) {
    free(arena_buf);
    return -1;
  }

  int stage_count = 0;
  if (parse_cmdline(cmdline, arena_buf, stages, &stage_count) < 0) {
    free(stages);
    free(arena_buf);
    return -1;
  }

  if (stage_count == 0) {
    free(stages);
    free(arena_buf);
    return 0;
  }

  // Validate and load manifests for all stages upfront
  eos_stage_exec_t *exec_stages =
      calloc(stage_count, sizeof(eos_stage_exec_t));
  if (!exec_stages) {
    free(stages);
    free(arena_buf);
    return -1;
  }

  for (int i = 0; i < stage_count; i++) {
    if (load_bin_manifest(stages[i].argv[0], &exec_stages[i].manifest) < 0) {
      free(exec_stages);
      free(stages);
      free(arena_buf);
      return -1;
    }
    exec_stages[i].argc = stages[i].argc;
    exec_stages[i].argv = stages[i].argv;
    exec_stages[i].pipe_in_fd = -1;
    exec_stages[i].pipe_out_fd = -1;
    exec_stages[i].in_file = stages[i].in_file;
    exec_stages[i].out_file = stages[i].out_file;
    exec_stages[i].err_file = stages[i].err_file;
    exec_stages[i].out_append = stages[i].out_append;
    exec_stages[i].err_append = stages[i].err_append;
    exec_stages[i].err_to_out = stages[i].err_to_out;
    exec_stages[i].out_to_err = stages[i].out_to_err;
  }

  // Fast path: single command without pipeline runs in the caller's thread
  // (preserving in-process state mutations for built-ins like `cd`)
  if (stage_count == 1) {
    eos_tctx_t *tctx = eos_tctx_get();
    int saved_map[3] = {-1, -1, -1};
    if (tctx) {
      for (int i = 0; i < 3; i++)
        saved_map[i] = tctx->fd_map[i];
    }

    fflush(stdout);
    fflush(stderr);

    int opened_fds[3] = {-1, -1, -1};
    int ret = -1;
    if (apply_stage_redirections(&exec_stages[0], opened_fds) == 0) {
      ret = exec_stages[0].manifest.entry_point(exec_stages[0].argc,
                                                exec_stages[0].argv);
    }

    fflush(stdout);
    fflush(stderr);
    clearerr(stdin);

    if (tctx) {
      for (int i = 0; i < 3; i++)
        tctx->fd_map[i] = saved_map[i];
    }

    close_stage_fds(opened_fds, -1, -1);
    free(exec_stages);
    free(stages);
    free(arena_buf);
    return ret;
  }

  // Multi-stage pipeline: create pipes via pipefs between adjacent stages
  eos_tctx_t *parent_tctx = eos_tctx_get();
  for (int i = 0; i < stage_count - 1; i++) {
    int pfd[2] = {-1, -1};
    if (pipe(pfd) < 0) {
      EOS_LOGE("eos_system() pipe() failed: %s", strerror(errno));
      for (int j = 0; j < stage_count; j++) {
        if (exec_stages[j].pipe_in_fd >= 0) {
          if (parent_tctx)
            eos_tctx_reg_fd(exec_stages[j].pipe_in_fd, parent_tctx);
          close(exec_stages[j].pipe_in_fd);
        }
        if (exec_stages[j].pipe_out_fd >= 0) {
          if (parent_tctx)
            eos_tctx_reg_fd(exec_stages[j].pipe_out_fd, parent_tctx);
          close(exec_stages[j].pipe_out_fd);
        }
      }
      free(exec_stages);
      free(stages);
      free(arena_buf);
      return -1;
    }
    // Detach pipe fds from parent thread context so child stage threads own them
    if (parent_tctx) {
      eos_tctx_unreg_fd(pfd[0], parent_tctx);
      eos_tctx_unreg_fd(pfd[1], parent_tctx);
    }
    exec_stages[i].pipe_out_fd = pfd[1];
    exec_stages[i + 1].pipe_in_fd = pfd[0];
  }

  fflush(stdout);
  fflush(stderr);

  pthread_t threads[EOS_SYSTEM_MAX_STAGES] = {0};
  bool spawned[EOS_SYSTEM_MAX_STAGES] = {false};

  for (int i = 0; i < stage_count; i++) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    int rc = pthread_create(&threads[i], &attr, pipeline_stage_thread,
                            &exec_stages[i]);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
      EOS_LOGE("eos_system() failed to spawn thread for stage %d", i);
      if (exec_stages[i].pipe_in_fd >= 0) {
        if (parent_tctx)
          eos_tctx_reg_fd(exec_stages[i].pipe_in_fd, parent_tctx);
        close(exec_stages[i].pipe_in_fd);
      }
      if (exec_stages[i].pipe_out_fd >= 0) {
        if (parent_tctx)
          eos_tctx_reg_fd(exec_stages[i].pipe_out_fd, parent_tctx);
        close(exec_stages[i].pipe_out_fd);
      }
    } else {
      spawned[i] = true;
    }
  }

  int last_status = -1;
  for (int i = 0; i < stage_count; i++) {
    if (spawned[i]) {
      void *retval = NULL;
      if (pthread_join(threads[i], &retval) == 0) {
        last_status = (int)(intptr_t)retval;
      }
    }
  }

  free(exec_stages);
  free(stages);
  free(arena_buf);
  return last_status;
}

int eos_system(const char *cmdline) {
  return __wrap_system(cmdline);
}
