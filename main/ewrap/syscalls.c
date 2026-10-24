#include "ecore/binfs.h"
#include "ecore/pipefs.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <reent.h>
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
  if (!name) {
    strlcpy(out, "", len);
    return;
  }

  // If absolute path or contains '/', use directly
  if (name[0] == '/' || strchr(name, '/')) {
    strlcpy(out, name, len);
    return;
  }

  // Get PATH environment variable (handled by system)
  char *path_env = getenv("PATH");
  if (!path_env || !path_env[0]) {
    path_env = "/bin";
  }

  // Iterate through colon-separated PATH directories
  char path_buf[EOS_MID_STR_LEN];
  strlcpy(path_buf, path_env, sizeof(path_buf));

  char *saveptr = NULL;
  char *dir = strtok_r(path_buf, ":", &saveptr);
  while (dir) {
    char candidate[EOS_MID_STR_LEN];
    snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);

    // Test if file exists / can be opened
    FILE *f = fopen(candidate, "r");
    if (f) {
      fclose(f);
      strlcpy(out, candidate, len);
      return;
    }

    dir = strtok_r(NULL, ":", &saveptr);
  }

  // Default to first PATH directory combined with name if not found in PATH
  strlcpy(path_buf, path_env, sizeof(path_buf));
  dir = strtok_r(path_buf, ":", &saveptr);
  if (dir) {
    snprintf(out, len, "%s/%s", dir, name);
  } else {
    strlcpy(out, name, len);
  }
}

static void expand_env_vars(const char **src_ptr, char **arena_ptr, char quote) {
  char *p = (char *)*src_ptr;
  char *w = *arena_ptr;

  if (*p == '?') {
    p++;
    eos_tctx_t *tctx = eos_tctx_get();
    int status = tctx ? tctx->last_exit_status : 0;
    int len = snprintf(w, 32, "%d", status);
    w += len;
    *src_ptr = p;
    *arena_ptr = w;
    return;
  }

  if (*p == '{') {
    p++;
    char *name_start = p;
    while (*p && *p != '}') p++;
    size_t name_len = p - name_start;
    if (*p == '}') p++;

    char name_buf[64];
    if (name_len < sizeof(name_buf)) {
      memcpy(name_buf, name_start, name_len);
      name_buf[name_len] = '\0';
      char *val = eos_tctx_getenv(name_buf);
      if (val) {
        size_t vlen = strlen(val);
        memcpy(w, val, vlen);
        w += vlen;
      }
    }
    *src_ptr = p;
    *arena_ptr = w;
    return;
  }

  char name_buf[64];
  int i = 0;
  while (*p && (isalnum((unsigned char)*p) || *p == '_') && i < (int)sizeof(name_buf) - 1) {
    name_buf[i++] = *p++;
  }
  name_buf[i] = '\0';
  if (i > 0) {
    char *val = eos_tctx_getenv(name_buf);
    if (val) {
      size_t vlen = strlen(val);
      memcpy(w, val, vlen);
      w += vlen;
    }
  } else {
    *w++ = '$';
  }
  *src_ptr = p;
  *arena_ptr = w;
}

// Reads the next shell token from *cursor, handling single/double quotes,
// environment variable expansion, and operator tokens (&&, ||, ;, &, |, <, >, etc.).
static char *next_shell_token(char **cursor, char **arena, bool *is_op) {
  char *p = *cursor;
  while (*p && isspace((unsigned char)*p))
    p++;

  if (!*p) {
    *cursor = p;
    return NULL;
  }

  *is_op = false;

  // Check operators: &&, ||, ;, &, |, etc.
  if (strncmp(p, "&&", 2) == 0) {
    *is_op = true;
    *cursor = p + 2;
    return "&&";
  }
  if (strncmp(p, "||", 2) == 0) {
    *is_op = true;
    *cursor = p + 2;
    return "||";
  }
  if (*p == ';') {
    *is_op = true;
    *cursor = p + 1;
    return ";";
  }
  if (*p == '&' && p[1] != '>' && p[1] != '>') {
    *is_op = true;
    *cursor = p + 1;
    return "&";
  }
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

  // Regular argument (supports quotes, escapes, and env expansion)
  char *out = *arena;
  char *w = out;

  while (*p && !isspace((unsigned char)*p)) {
    if (*p == '|' || *p == '<' || *p == '>' || *p == ';' || *p == '&')
      break;
    if (strncmp(p, "&&", 2) == 0 || strncmp(p, "||", 2) == 0)
      break;
    if ((p[0] == '1' || p[0] == '2') && p[1] == '>')
      break;
    if (p[0] == '&' && p[1] == '>')
      break;

    if (*p == '\'' || *p == '"') {
      char quote = *p++;
      while (*p && *p != quote) {
        if (quote == '"' && *p == '$' && p[1]) {
          p++;
          expand_env_vars((const char **)&p, &w, '"');
          continue;
        }
        if (quote == '"' && *p == '\\' && p[1])
          p++;
        *w++ = *p++;
      }
      if (*p == quote)
        p++;
    } else if (*p == '$' && p[1]) {
      p++;
      expand_env_vars((const char **)&p, &w, '\0');
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

typedef struct {
  char *text;
  bool is_op;
} eos_token_t;

typedef enum {
  EOS_NODE_COMMAND,
  EOS_NODE_SEQ,
  EOS_NODE_AND,
  EOS_NODE_OR,
  EOS_NODE_BG
} eos_node_type_t;

typedef struct eos_ast_node_t {
  eos_node_type_t type;
  struct eos_ast_node_t *left;
  struct eos_ast_node_t *right;
  int stage_count;
  eos_cmd_stage_t stages[EOS_SYSTEM_MAX_STAGES];
} eos_ast_node_t;

static bool get_op_bp(const char *op, int *bp) {
  if (strcmp(op, ";") == 0 || strcmp(op, "&") == 0) {
    *bp = 10;
    return true;
  }
  if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
    *bp = 20;
    return true;
  }
  return false;
}

static eos_ast_node_t *parse_command(eos_token_t *tokens, int num_tokens, int *idx) {
  eos_ast_node_t *node = calloc(1, sizeof(eos_ast_node_t));
  if (!node) return NULL;
  node->type = EOS_NODE_COMMAND;

  int stage_idx = 0;
  memset(node->stages, 0, sizeof(eos_cmd_stage_t) * EOS_SYSTEM_MAX_STAGES);

  while (*idx < num_tokens) {
    eos_token_t *tok = &tokens[*idx];
    if (tok->is_op) {
      if (strcmp(tok->text, "|") == 0) {
        (*idx)++;
        if (node->stages[stage_idx].argc == 0) {
          EOS_LOGE("eos_system() syntax error: empty pipe stage");
          free(node);
          return NULL;
        }
        if (stage_idx + 1 >= EOS_SYSTEM_MAX_STAGES) {
          EOS_LOGE("eos_system() too many pipeline stages");
          free(node);
          return NULL;
        }
        node->stages[stage_idx].argv[node->stages[stage_idx].argc] = NULL;
        stage_idx++;
        continue;
      } else if (strcmp(tok->text, "<") == 0 || strcmp(tok->text, ">") == 0 ||
                 strcmp(tok->text, ">>") == 0 || strcmp(tok->text, "2>") == 0 ||
                 strcmp(tok->text, "2>>") == 0 || strcmp(tok->text, "&>") == 0 ||
                 strcmp(tok->text, "&>>") == 0) {
        eos_cmd_stage_t *st = &node->stages[stage_idx];
        char *op = tok->text;
        (*idx)++;
        if (*idx >= num_tokens || tokens[*idx].is_op) {
          EOS_LOGE("eos_system() syntax error: missing target for '%s'", op);
          free(node);
          return NULL;
        }
        char *target = tokens[*idx].text;
        (*idx)++;

        if (strcmp(op, "<") == 0) {
          st->in_file = target;
        } else if (strcmp(op, ">") == 0) {
          st->out_file = target;
          st->out_append = false;
        } else if (strcmp(op, ">>") == 0) {
          st->out_file = target;
          st->out_append = true;
        } else if (strcmp(op, "2>") == 0) {
          st->err_file = target;
          st->err_append = false;
        } else if (strcmp(op, "2>>") == 0) {
          st->err_file = target;
          st->err_append = true;
        } else if (strcmp(op, "&>") == 0) {
          st->out_file = target;
          st->out_append = false;
          st->err_to_out = true;
        } else if (strcmp(op, "&>>") == 0) {
          st->out_file = target;
          st->out_append = true;
          st->err_to_out = true;
        }
        continue;
      } else if (strcmp(tok->text, "2>&1") == 0) {
        node->stages[stage_idx].err_to_out = true;
        node->stages[stage_idx].out_to_err = false;
        (*idx)++;
        continue;
      } else if (strcmp(tok->text, "1>&2") == 0) {
        node->stages[stage_idx].out_to_err = true;
        node->stages[stage_idx].err_to_out = false;
        (*idx)++;
        continue;
      } else {
        break;
      }
    } else {
      eos_cmd_stage_t *st = &node->stages[stage_idx];
      if (st->argc + 1 >= EOS_SYSTEM_MAX_ARGS) {
        EOS_LOGE("eos_system() too many arguments");
        free(node);
        return NULL;
      }
      st->argv[st->argc++] = tok->text;
      (*idx)++;
    }
  }

  node->stages[stage_idx].argv[node->stages[stage_idx].argc] = NULL;
  if (stage_idx > 0 && node->stages[stage_idx].argc == 0) {
    EOS_LOGE("eos_system() syntax error: trailing pipe '|'");
    free(node);
    return NULL;
  }

  node->stage_count = (node->stages[0].argc == 0) ? 0 : (stage_idx + 1);
  return node;
}

static eos_ast_node_t *parse_expr(eos_token_t *tokens, int num_tokens, int *idx, int min_bp) {
  eos_ast_node_t *left = parse_command(tokens, num_tokens, idx);
  if (!left) return NULL;

  while (*idx < num_tokens) {
    eos_token_t *tok = &tokens[*idx];
    if (!tok->is_op) break;

    int bp = 0;
    if (!get_op_bp(tok->text, &bp) || bp < min_bp) break;

    char *op = tok->text;
    (*idx)++;

    eos_ast_node_t *right = parse_expr(tokens, num_tokens, idx, bp);
    if (!right) {
      return left;
    }

    eos_ast_node_t *node = calloc(1, sizeof(eos_ast_node_t));
    if (!node) return left;

    if (strcmp(op, ";") == 0) {
      node->type = EOS_NODE_SEQ;
    } else if (strcmp(op, "&&") == 0) {
      node->type = EOS_NODE_AND;
    } else if (strcmp(op, "||") == 0) {
      node->type = EOS_NODE_OR;
    } else if (strcmp(op, "&") == 0) {
      node->type = EOS_NODE_BG;
    }

    node->left = left;
    node->right = right;
    left = node;
  }

  return left;
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

  // Re-bind FILE streams to match redirected FDs (Newlib/Picolibc reentrancy support)
  fflush(stdout);
  fflush(stderr);

  FILE *new_stdin = fdopen(STDIN_FILENO, "r");
  FILE *new_stdout = fdopen(STDOUT_FILENO, "w");
  FILE *new_stderr = fdopen(STDERR_FILENO, "w");

  if (new_stdin) {
    stdin = new_stdin;
    setvbuf(stdin, NULL, _IONBF, 0);
  }
  if (new_stdout) {
    stdout = new_stdout;
    setvbuf(stdout, NULL, _IONBF, 0);
  }
  if (new_stderr) {
    stderr = new_stderr;
    setvbuf(stderr, NULL, _IONBF, 0);
  }

#ifdef _GLOBAL_REENT
  if (new_stdin) _GLOBAL_REENT->_stdin = new_stdin;
  if (new_stdout) _GLOBAL_REENT->_stdout = new_stdout;
  if (new_stderr) _GLOBAL_REENT->_stderr = new_stderr;
#endif

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

  FILE *saved_stdin = stdin;
  FILE *saved_stdout = stdout;
  FILE *saved_stderr = stderr;

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

  // Restore stdio streams
  fflush(stdout);
  fflush(stderr);
  stdin = saved_stdin;
  stdout = saved_stdout;
  stderr = saved_stderr;
#ifdef _GLOBAL_REENT
  _GLOBAL_REENT->_stdin = stdin;
  _GLOBAL_REENT->_stdout = stdout;
  _GLOBAL_REENT->_stderr = stderr;
#endif

  // Clear thread-local stdio mappings before closing underlying fds
  if (tctx) {
    tctx->fd_map[STDIN_FILENO] = -1;
    tctx->fd_map[STDOUT_FILENO] = -1;
    tctx->fd_map[STDERR_FILENO] = -1;
  }

  close_stage_fds(opened_fds, st->pipe_in_fd, st->pipe_out_fd);
  return (void *)(intptr_t)ret;
}

static int eval_ast(eos_ast_node_t *node);

typedef struct {
  eos_ast_node_t *node;
} bg_wrapper_t;

static void *bg_thread_entry(void *arg) {
  bg_wrapper_t *bg = (bg_wrapper_t *)arg;
  eval_ast(bg->node);
  free(bg);
  return NULL;
}

static bool check_shebang_script(const char *cmd, char *interp_out, size_t interp_out_len, char *interp_arg_out, size_t interp_arg_out_len) {
  char path[EOS_MID_STR_LEN];
  resolve_path(cmd, path, sizeof(path));

  FILE *f = fopen(path, "r");
  if (!f) return false;

  char line[256];
  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    return false;
  }
  fclose(f);

  if (line[0] != '#' || line[1] != '!') {
    return false;
  }

  char *nl = strchr(line, '\n');
  if (nl) *nl = '\0';
  char *cr = strchr(line, '\r');
  if (cr) *cr = '\0';

  char *interp = line + 2;
  while (*interp == ' ' || *interp == '\t') interp++;

  char *arg = interp;
  while (*arg && *arg != ' ' && *arg != '\t') arg++;
  if (*arg) {
    *arg = '\0';
    arg++;
    while (*arg == ' ' || *arg == '\t') arg++;
  } else {
    arg = NULL;
  }

  snprintf(interp_out, interp_out_len, "%s", interp);
  if (interp_arg_out) {
    if (arg && *arg) {
      snprintf(interp_arg_out, interp_arg_out_len, "%s", arg);
    } else {
      interp_arg_out[0] = '\0';
    }
  }
  return true;
}

static int eval_command_node(eos_ast_node_t *node) {
  int stage_count = node->stage_count;
  if (stage_count == 0) return 0;

  eos_stage_exec_t *exec_stages = calloc(stage_count, sizeof(eos_stage_exec_t));
  if (!exec_stages) return -1;

  bool shebang[EOS_SYSTEM_MAX_STAGES] = {false};

  for (int i = 0; i < stage_count; i++) {
    char interp[EOS_SMALL_STR_LEN];
    char interp_arg[EOS_SMALL_STR_LEN];
    int stage_argc = node->stages[i].argc;
    char **stage_argv = node->stages[i].argv;
    bool shebang_allocated = false;

    if (check_shebang_script(stage_argv[0], interp, sizeof(interp), interp_arg, sizeof(interp_arg))) {
      int extra = (interp_arg[0] != '\0') ? 2 : 1;
      char **new_argv = calloc(stage_argc + extra + 1, sizeof(char *));
      if (new_argv) {
        int idx = 0;
        new_argv[idx++] = strdup(interp);
        if (interp_arg[0] != '\0') {
          new_argv[idx++] = strdup(interp_arg);
        }
        new_argv[idx++] = strdup(stage_argv[0]);
        for (int j = 1; j < stage_argc; j++) {
          new_argv[idx++] = strdup(stage_argv[j]);
        }
        new_argv[idx] = NULL;
        stage_argv = new_argv;
        stage_argc = stage_argc + extra;
        shebang_allocated = true;
        shebang[i] = true;
      }
    }

    if (load_bin_manifest(stage_argv[0], &exec_stages[i].manifest) < 0) {
      if (shebang_allocated) {
        for (int j = 0; j < stage_argc; j++) free(stage_argv[j]);
        free(stage_argv);
      }
      for (int j = 0; j < i; j++) {
        if (shebang[j]) {
          for (int k = 0; k < exec_stages[j].argc; k++) free(exec_stages[j].argv[k]);
          free(exec_stages[j].argv);
        }
      }
      free(exec_stages);
      return -1;
    }
    exec_stages[i].argc = stage_argc;
    exec_stages[i].argv = stage_argv;
    exec_stages[i].pipe_in_fd = -1;
    exec_stages[i].pipe_out_fd = -1;
    exec_stages[i].in_file = node->stages[i].in_file;
    exec_stages[i].out_file = node->stages[i].out_file;
    exec_stages[i].err_file = node->stages[i].err_file;
    exec_stages[i].out_append = node->stages[i].out_append;
    exec_stages[i].err_append = node->stages[i].err_append;
    exec_stages[i].err_to_out = node->stages[i].err_to_out;
    exec_stages[i].out_to_err = node->stages[i].out_to_err;
  }

  int ret = 0;

  if (stage_count == 1) {
    eos_tctx_t *tctx = eos_tctx_get();
    int saved_map[3] = {-1, -1, -1};
    FILE *saved_stdin = stdin;
    FILE *saved_stdout = stdout;
    FILE *saved_stderr = stderr;
    if (tctx) {
      for (int i = 0; i < 3; i++)
        saved_map[i] = tctx->fd_map[i];
    }

    fflush(stdout);
    fflush(stderr);

    int opened_fds[3] = {-1, -1, -1};
    if (apply_stage_redirections(&exec_stages[0], opened_fds) == 0) {
      ret = exec_stages[0].manifest.entry_point(exec_stages[0].argc,
                                                exec_stages[0].argv);
    }

    fflush(stdout);
    fflush(stderr);
    clearerr(stdin);

    stdin = saved_stdin;
    stdout = saved_stdout;
    stderr = saved_stderr;
#ifdef _GLOBAL_REENT
    _GLOBAL_REENT->_stdin = stdin;
    _GLOBAL_REENT->_stdout = stdout;
    _GLOBAL_REENT->_stderr = stderr;
#endif

    if (tctx) {
      for (int i = 0; i < 3; i++)
        tctx->fd_map[i] = saved_map[i];
    }

    close_stage_fds(opened_fds, -1, -1);
  } else {
    eos_tctx_t *parent_tctx = eos_tctx_get();
    for (int i = 0; i < stage_count - 1; i++) {
      int pfd[2] = {-1, -1};
      if (pipe(pfd) < 0) {
        EOS_LOGE("eos_system() pipe() failed: %s", strerror(errno));
        for (int j = 0; j < stage_count; j++) {
          if (exec_stages[j].pipe_in_fd >= 0) {
            if (parent_tctx) eos_tctx_reg_fd(exec_stages[j].pipe_in_fd, parent_tctx);
            close(exec_stages[j].pipe_in_fd);
          }
          if (exec_stages[j].pipe_out_fd >= 0) {
            if (parent_tctx) eos_tctx_reg_fd(exec_stages[j].pipe_out_fd, parent_tctx);
            close(exec_stages[j].pipe_out_fd);
          }
        }
        for (int j = 0; j < stage_count; j++) {
          if (shebang[j]) {
            for (int k = 0; k < exec_stages[j].argc; k++) free(exec_stages[j].argv[k]);
            free(exec_stages[j].argv);
          }
        }
        free(exec_stages);
        return -1;
      }
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

      int rc = pthread_create(&threads[i], &attr, pipeline_stage_thread, &exec_stages[i]);
      pthread_attr_destroy(&attr);

      if (rc != 0) {
        EOS_LOGE("eos_system() failed to spawn thread for stage %d", i);
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
    ret = last_status;
  }

  for (int i = 0; i < stage_count; i++) {
    if (shebang[i]) {
      for (int j = 0; j < exec_stages[i].argc; j++) {
        free(exec_stages[i].argv[j]);
      }
      free(exec_stages[i].argv);
    }
  }
  free(exec_stages);
  return ret;
}

static int eval_ast(eos_ast_node_t *node) {
  if (!node) return 0;

  int status = 0;
  eos_tctx_t *tctx = eos_tctx_get();

  switch (node->type) {
    case EOS_NODE_COMMAND:
      status = eval_command_node(node);
      break;
    case EOS_NODE_SEQ:
      eval_ast(node->left);
      status = eval_ast(node->right);
      break;
    case EOS_NODE_AND:
      status = eval_ast(node->left);
      if (status == 0) {
        status = eval_ast(node->right);
      }
      break;
    case EOS_NODE_OR:
      status = eval_ast(node->left);
      if (status != 0) {
        status = eval_ast(node->right);
      }
      break;
    case EOS_NODE_BG: {
      bg_wrapper_t *bg = malloc(sizeof(bg_wrapper_t));
      if (bg) {
        bg->node = node->left;
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &attr, bg_thread_entry, bg) != 0) {
          free(bg);
        }
        pthread_attr_destroy(&attr);
      }
      status = 0;
      break;
    }
  }

  if (tctx) {
    tctx->last_exit_status = status;
  }
  return status;
}

static void free_ast(eos_ast_node_t *node) {
  if (!node) return;
  free_ast(node->left);
  free_ast(node->right);
  free(node);
}

int __wrap_system(const char *cmdline) {
  if (!cmdline) {
    EOS_LOGE("eos_system() called with null cmdline");
    return -1;
  }

  size_t cmd_len = strlen(cmdline);
  char *arena_buf = malloc(cmd_len * 2 + 64);
  if (!arena_buf) {
    return -1;
  }

  int max_tokens = 128;
  eos_token_t *tokens = calloc(max_tokens, sizeof(eos_token_t));
  if (!tokens) {
    free(arena_buf);
    return -1;
  }

  char *cursor = (char *)cmdline;
  char *arena = arena_buf;
  bool is_op = false;
  char *tok = NULL;
  int num_tokens = 0;

  while ((tok = next_shell_token(&cursor, &arena, &is_op)) != NULL) {
    if (num_tokens >= max_tokens - 1) {
      break;
    }
    tokens[num_tokens].text = tok;
    tokens[num_tokens].is_op = is_op;
    num_tokens++;
  }

  if (num_tokens == 0) {
    free(tokens);
    free(arena_buf);
    return 0;
  }

  int idx = 0;
  eos_ast_node_t *root = parse_expr(tokens, num_tokens, &idx, 0);
  int ret = 0;
  if (root) {
    ret = eval_ast(root);
    free_ast(root);
  } else {
    ret = -1;
  }

  free(tokens);
  free(arena_buf);
  return ret;
}

int eos_system(const char *cmdline) {
  return __wrap_system(cmdline);
}
