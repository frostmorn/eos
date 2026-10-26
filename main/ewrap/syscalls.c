#include "ecore/binfs.h"
#include "ecore/pipefs.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <reent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// This call isn't implemented in picolib, but cause it have a declaration
// burried somewhere in <stdlib.h> why not to use it, right?
// components/esp_lib/src/syscalls.c
extern int __real_system(const char *str);

#define EOS_SYSTEM_MAX_STAGES 8
#define EOS_SYSTEM_MAX_ARGS   128

typedef struct {
  int argc;
  char *argv[EOS_SYSTEM_MAX_ARGS];
  char *in_file;
  char *out_file;
  bool out_append;
} eos_cmd_stage_t;

typedef struct {
  eos_bin_t manifest;
  bool is_builtin;
  int argc;
  char **argv;
  int pipe_in_fd;
  int pipe_out_fd;
  char *in_file;
  char *out_file;
  bool out_append;
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

static void expand_env_vars(const char **src_ptr, char **arena_ptr,
                            const char *arena_end, char quote) {
  (void)quote;
  char *p = (char *)*src_ptr;
  char *w = *arena_ptr;

  if (*p == '?') {
    p++;
    eos_tctx_t *tctx = eos_tctx_get();
    int status = tctx ? tctx->last_exit_status : 0;
    size_t avail = (w < arena_end) ? (size_t)(arena_end - w) : 0;
    if (avail > 1) {
      int len = snprintf(w, avail, "%d", status);
      if (len > 0) {
        w += ((size_t)len < avail) ? (size_t)len : (avail - 1);
      }
    }
    *src_ptr = p;
    *arena_ptr = w;
    return;
  }

  if (*p == '{') {
    p++;
    char *name_start = p;
    while (*p && *p != '}')
      p++;
    size_t name_len = (size_t)(p - name_start);
    if (*p == '}')
      p++;

    char name_buf[64];
    if (name_len < sizeof(name_buf)) {
      memcpy(name_buf, name_start, name_len);
      name_buf[name_len] = '\0';
      char *val = eos_tctx_getenv(name_buf);
      if (val) {
        size_t vlen = strlen(val);
        size_t avail = (w < arena_end) ? (size_t)(arena_end - w) : 0;
        if (vlen > avail)
          vlen = avail;
        if (vlen > 0) {
          memcpy(w, val, vlen);
          w += vlen;
        }
      }
    }
    *src_ptr = p;
    *arena_ptr = w;
    return;
  }

  char name_buf[64];
  int i = 0;
  while (*p && (isalnum((unsigned char)*p) || *p == '_') &&
         i < (int)sizeof(name_buf) - 1) {
    name_buf[i++] = *p++;
  }
  name_buf[i] = '\0';
  if (i > 0) {
    char *val = eos_tctx_getenv(name_buf);
    if (val) {
      size_t vlen = strlen(val);
      size_t avail = (w < arena_end) ? (size_t)(arena_end - w) : 0;
      if (vlen > avail)
        vlen = avail;
      if (vlen > 0) {
        memcpy(w, val, vlen);
        w += vlen;
      }
    }
  } else if (w < arena_end) {
    *w++ = '$';
  }
  *src_ptr = p;
  *arena_ptr = w;
}

// Reads the next shell token from *cursor, handling single/double quotes,
// environment variable expansion, and operator tokens (&&, ||, ;, &, |, <, >, etc.).
static char *next_shell_token(char **cursor, char **arena,
                              const char *arena_end, bool *is_op,
                              bool *has_wildcard) {
  char *p = *cursor;
  while (*p && isspace((unsigned char)*p))
    p++;

  if (!*p || *arena >= arena_end) {
    *cursor = p;
    return NULL;
  }

  *is_op = false;
  *has_wildcard = false;

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
          expand_env_vars((const char **)&p, &w, arena_end, '"');
          continue;
        }
        if (quote == '"' && *p == '\\' && p[1])
          p++;
        if (w < arena_end) {
          *w++ = *p;
        }
        p++;
      }
      if (*p == quote)
        p++;
    } else if (*p == '$' && p[1]) {
      p++;
      char *before = w;
      expand_env_vars((const char **)&p, &w, arena_end, '\0');
      for (char *c = before; c < w; c++) {
        if (*c == '*' || *c == '?' || *c == '[') {
          *has_wildcard = true;
          break;
        }
      }
    } else if (*p == '\\' && p[1]) {
      p++;
      if (w < arena_end) {
        *w++ = *p;
      }
      p++;
    } else {
      if (*p == '*' || *p == '?' || *p == '[') {
        *has_wildcard = true;
      }
      if (w < arena_end) {
        *w++ = *p;
      }
      p++;
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
  bool has_wildcard;
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
  char *glob_strings[EOS_SYSTEM_MAX_ARGS];
  int glob_string_count;
} eos_ast_node_t;

static void free_ast_node_single(eos_ast_node_t *node) {
  if (!node) return;
  for (int i = 0; i < node->glob_string_count; i++) {
    if (node->glob_strings[i]) {
      free(node->glob_strings[i]);
    }
  }
  free(node);
}

typedef struct {
  char **paths;
  int count;
  int cap;
} glob_result_t;

static void glob_add_path(glob_result_t *res, const char *path) {
  if (res->count >= EOS_SYSTEM_MAX_ARGS) return;
  if (res->count >= res->cap) {
    int new_cap = res->cap ? res->cap * 2 : 16;
    char **new_paths = realloc(res->paths, new_cap * sizeof(char *));
    if (!new_paths) return;
    res->paths = new_paths;
    res->cap = new_cap;
  }
  res->paths[res->count++] = strdup(path);
}

static int glob_cmp(const void *a, const void *b) {
  const char *sa = *(const char *const *)a;
  const char *sb = *(const char *const *)b;
  return strcmp(sa, sb);
}

static void glob_recurse(const char *base_dir, const char *rem_pat, glob_result_t *res) {
  while (rem_pat && *rem_pat == '/') {
    rem_pat++;
  }
  if (!rem_pat || !*rem_pat) {
    if (base_dir && *base_dir) {
      glob_add_path(res, base_dir);
    }
    return;
  }

  char segment[128];
  const char *slash = strchr(rem_pat, '/');
  const char *next_rem = NULL;

  if (slash) {
    size_t seg_len = (size_t)(slash - rem_pat);
    if (seg_len >= sizeof(segment)) return;
    memcpy(segment, rem_pat, seg_len);
    segment[seg_len] = '\0';
    next_rem = slash + 1;
    while (*next_rem == '/') next_rem++;
  } else {
    strncpy(segment, rem_pat, sizeof(segment) - 1);
    segment[sizeof(segment) - 1] = '\0';
    next_rem = NULL;
  }

  bool has_wildcard = (strchr(segment, '*') || strchr(segment, '?') || strchr(segment, '['));

  if (!has_wildcard) {
    char new_base[EOS_LARGE_STR_LEN];
    if (!base_dir || !*base_dir) {
      snprintf(new_base, sizeof(new_base), "%s", segment);
    } else if (strcmp(base_dir, "/") == 0) {
      snprintf(new_base, sizeof(new_base), "/%s", segment);
    } else {
      snprintf(new_base, sizeof(new_base), "%s/%s", base_dir, segment);
    }
    glob_recurse(new_base, next_rem, res);
    return;
  }

  const char *dir_to_open = (base_dir && *base_dir) ? base_dir : ".";
  DIR *d = opendir((char *)dir_to_open);
  if (!d) return;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    if (ent->d_name[0] == '.' && segment[0] != '.') {
      continue;
    }

    if (fnmatch(segment, ent->d_name, 0) == 0) {
      char match_path[EOS_LARGE_STR_LEN];
      if (!base_dir || !*base_dir) {
        snprintf(match_path, sizeof(match_path), "%s", ent->d_name);
      } else if (strcmp(base_dir, "/") == 0) {
        snprintf(match_path, sizeof(match_path), "/%s", ent->d_name);
      } else {
        snprintf(match_path, sizeof(match_path), "%s/%s", base_dir, ent->d_name);
      }

      if (next_rem && *next_rem) {
        struct stat st;
        if (stat(match_path, &st) == 0 && S_ISDIR(st.st_mode)) {
          glob_recurse(match_path, next_rem, res);
        }
      } else {
        glob_add_path(res, match_path);
      }
    }
  }
  closedir(d);
}

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
          free_ast_node_single(node);
          return NULL;
        }
        if (stage_idx + 1 >= EOS_SYSTEM_MAX_STAGES) {
          EOS_LOGE("eos_system() too many pipeline stages");
          free_ast_node_single(node);
          return NULL;
        }
        node->stages[stage_idx].argv[node->stages[stage_idx].argc] = NULL;
        stage_idx++;
        continue;
      } else if (strcmp(tok->text, "<") == 0 || strcmp(tok->text, ">") == 0 ||
                 strcmp(tok->text, ">>") == 0) {
        eos_cmd_stage_t *st = &node->stages[stage_idx];
        char *op = tok->text;
        (*idx)++;
        if (*idx >= num_tokens || tokens[*idx].is_op) {
          EOS_LOGE("eos_system() syntax error: missing target for '%s'", op);
          free_ast_node_single(node);
          return NULL;
        }
        char *target = tokens[*idx].text;
        if (tokens[*idx].has_wildcard) {
          glob_result_t gres = {0};
          const char *pat = target;
          const char *base = NULL;
          if (*pat == '/') {
            base = "/";
            while (*pat == '/') pat++;
          }
          glob_recurse(base, pat, &gres);
          if (gres.count == 1) {
            target = gres.paths[0];
            if (node->glob_string_count < EOS_SYSTEM_MAX_ARGS) {
              node->glob_strings[node->glob_string_count++] = target;
            }
          } else {
            for (int g = 0; g < gres.count; g++) {
              free(gres.paths[g]);
            }
          }
          free(gres.paths);
        }
        (*idx)++;

        if (strcmp(op, "<") == 0) {
          st->in_file = target;
        } else if (strcmp(op, ">") == 0) {
          st->out_file = target;
          st->out_append = false;
        } else if (strcmp(op, ">>") == 0) {
          st->out_file = target;
          st->out_append = true;
        }
        continue;
      } else {
        break;
      }
    } else {
      eos_cmd_stage_t *st = &node->stages[stage_idx];
      if (tok->has_wildcard) {
        glob_result_t gres = {0};
        const char *pat = tok->text;
        const char *base = NULL;
        if (*pat == '/') {
          base = "/";
          while (*pat == '/') pat++;
        }
        glob_recurse(base, pat, &gres);
        if (gres.count > 0) {
          if (gres.count > 1) {
            qsort(gres.paths, gres.count, sizeof(char *), glob_cmp);
          }
          for (int g = 0; g < gres.count; g++) {
            if (st->argc + 1 >= EOS_SYSTEM_MAX_ARGS ||
                node->glob_string_count >= EOS_SYSTEM_MAX_ARGS) {
              free(gres.paths[g]);
              continue;
            }
            node->glob_strings[node->glob_string_count++] = gres.paths[g];
            st->argv[st->argc++] = gres.paths[g];
          }
          free(gres.paths);
        } else {
          // If no matches found, keep pattern literal as POSIX/Linux standard
          if (st->argc + 1 >= EOS_SYSTEM_MAX_ARGS) {
            EOS_LOGE("eos_system() too many arguments");
            free_ast_node_single(node);
            return NULL;
          }
          st->argv[st->argc++] = tok->text;
        }
      } else {
        if (st->argc + 1 >= EOS_SYSTEM_MAX_ARGS) {
          EOS_LOGE("eos_system() too many arguments");
          free_ast_node_single(node);
          return NULL;
        }
        st->argv[st->argc++] = tok->text;
      }
      (*idx)++;
    }
  }

  node->stages[stage_idx].argv[node->stages[stage_idx].argc] = NULL;
  if (stage_idx > 0 && node->stages[stage_idx].argc == 0) {
    EOS_LOGE("eos_system() syntax error: trailing pipe '|'");
    free_ast_node_single(node);
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

    eos_ast_node_t *right = parse_expr(tokens, num_tokens, idx, bp + 1);
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
    EOS_LOGE("eos_system() not found: %s (errno=%d: %s)", path, errno, strerror(errno));
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

// Built-in shell environment commands (VAR=VALUE, export, setenv, unset, env)
static bool is_env_assignment_tok(const char *s) {
  if (!s || (!isalpha((unsigned char)s[0]) && s[0] != '_'))
    return false;
  const char *p = s + 1;
  while (*p && (isalnum((unsigned char)*p) || *p == '_'))
    p++;
  return (*p == '=');
}

static int builtin_env_entry(int argc, char **argv) {
  if (argc <= 0 || !argv || !argv[0])
    return 0;

  if (strcmp(argv[0], "env") == 0 || strcmp(argv[0], "printenv") == 0 ||
      (strcmp(argv[0], "export") == 0 && argc == 1)) {
    char **envp = eos_tctx_get_environ();
    if (envp) {
      for (int i = 0; envp[i] != NULL; i++) {
        printf("%s\n", envp[i]);
      }
      fflush(stdout);
    }
    return 0;
  }

  if (strcmp(argv[0], "export") == 0) {
    for (int i = 1; i < argc; i++) {
      char *eq = strchr(argv[i], '=');
      if (eq) {
        size_t klen = (size_t)(eq - argv[i]);
        char key[64];
        if (klen > 0 && klen < sizeof(key)) {
          memcpy(key, argv[i], klen);
          key[klen] = '\0';
          eos_tctx_setenv(key, eq + 1, 1);
        }
      } else if (i + 1 < argc) {
        eos_tctx_setenv(argv[i], argv[i + 1], 1);
        i++;
      }
    }
    return 0;
  }

  if (strcmp(argv[0], "setenv") == 0) {
    if (argc < 3) {
      printf("Usage: setenv <NAME> <VALUE>\n");
      return -1;
    }
    return eos_tctx_setenv(argv[1], argv[2], 1);
  }

  if (strcmp(argv[0], "unset") == 0 || strcmp(argv[0], "unsetenv") == 0) {
    for (int i = 1; i < argc; i++) {
      eos_tctx_unsetenv(argv[i]);
    }
    return 0;
  }

  // Direct assignment tokens: VAR=VALUE [VAR2=VALUE2 ...]
  for (int i = 0; i < argc; i++) {
    char *eq = strchr(argv[i], '=');
    if (eq) {
      size_t klen = (size_t)(eq - argv[i]);
      char key[64];
      if (klen > 0 && klen < sizeof(key)) {
        memcpy(key, argv[i], klen);
        key[klen] = '\0';
        eos_tctx_setenv(key, eq + 1, 1);
      }
    }
  }
  return 0;
}

static bool is_shell_builtin(int argc, char **argv) {
  if (argc <= 0 || !argv || !argv[0])
    return false;
  if (strcmp(argv[0], "export") == 0 || strcmp(argv[0], "setenv") == 0 ||
      strcmp(argv[0], "unset") == 0 || strcmp(argv[0], "unsetenv") == 0 ||
      strcmp(argv[0], "env") == 0 || strcmp(argv[0], "printenv") == 0) {
    return true;
  }
  for (int i = 0; i < argc; i++) {
    if (!is_env_assignment_tok(argv[i]))
      return false;
  }
  return true;
}

static bool has_stage_redirections(const eos_stage_exec_t *st) {
  if (!st) return false;
  return (st->pipe_in_fd >= 0 || st->pipe_out_fd >= 0 ||
          st->in_file != NULL || st->out_file != NULL);
}

static int apply_stage_redirections(const eos_stage_exec_t *st,
                                    int opened_fds[2]) {
  opened_fds[0] = -1;
  opened_fds[1] = -1;

  if (!has_stage_redirections(st)) {
    return 0;
  }

  // Flush any pending output before changing thread-local fd_map targets
  fflush(stdout);

  // 1. Pipe descriptors first (explicit file redirections override pipes)
  if (st->pipe_in_fd >= 0) {
    dup2(st->pipe_in_fd, STDIN_FILENO);
    if (stdin && fileno(stdin) >= 0) {
      dup2(st->pipe_in_fd, fileno(stdin));
    }
  }
  if (st->pipe_out_fd >= 0) {
    dup2(st->pipe_out_fd, STDOUT_FILENO);
    if (stdout && fileno(stdout) >= 0) {
      dup2(st->pipe_out_fd, fileno(stdout));
    }
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
    if (stdin && fileno(stdin) >= 0) {
      dup2(fd, fileno(stdin));
    }
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
    if (stdout && fileno(stdout) >= 0) {
      dup2(fd, fileno(stdout));
    }
  }

  clearerr(stdin);
  clearerr(stdout);

  return 0;
}

static void close_stage_fds(int opened_fds[2], int pipe_in_fd,
                            int pipe_out_fd) {
  fflush(stdout);
  clearerr(stdin);
  clearerr(stdout);

  for (int i = 0; i < 2; i++) {
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
  int opened_fds[2] = {-1, -1};
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

  fflush(stdout);
  fflush(stderr);

  // Clear thread-local stdio mappings before closing underlying fds
  if (tctx) {
    for (int i = 0; i < 16; i++) {
      tctx->fd_map[i] = -1;
    }
  }

  close_stage_fds(opened_fds, st->pipe_in_fd, st->pipe_out_fd);
  return (void *)(intptr_t)ret;
}

static int eval_ast(eos_ast_node_t *node);

static char *dup_str_untracked(const char *str, eos_tctx_t *parent_tctx) {
  if (!str) return NULL;
  char *copy = strdup(str);
  if (copy && parent_tctx) {
    eos_tctx_unreg_memblock(copy, parent_tctx);
  }
  return copy;
}

static eos_ast_node_t *clone_ast_node(const eos_ast_node_t *src,
                                      eos_tctx_t *parent_tctx) {
  if (!src) return NULL;
  eos_ast_node_t *dst = calloc(1, sizeof(eos_ast_node_t));
  if (!dst) return NULL;
  if (parent_tctx) {
    eos_tctx_unreg_memblock(dst, parent_tctx);
  }

  dst->type = src->type;
  dst->stage_count = src->stage_count;
  dst->left = clone_ast_node(src->left, parent_tctx);
  dst->right = clone_ast_node(src->right, parent_tctx);

  for (int i = 0; i < src->stage_count && i < EOS_SYSTEM_MAX_STAGES; i++) {
    const eos_cmd_stage_t *st_src = &src->stages[i];
    eos_cmd_stage_t *st_dst = &dst->stages[i];
    st_dst->argc = st_src->argc;
    for (int j = 0; j < st_src->argc && j < EOS_SYSTEM_MAX_ARGS - 1; j++) {
      st_dst->argv[j] = dup_str_untracked(st_src->argv[j], parent_tctx);
    }
    st_dst->argv[st_dst->argc] = NULL;
    st_dst->in_file = dup_str_untracked(st_src->in_file, parent_tctx);
    st_dst->out_file = dup_str_untracked(st_src->out_file, parent_tctx);
    st_dst->out_append = st_src->out_append;
  }
  return dst;
}

static void free_cloned_ast_node(eos_ast_node_t *node) {
  if (!node) return;
  free_cloned_ast_node(node->left);
  free_cloned_ast_node(node->right);
  for (int i = 0; i < node->stage_count && i < EOS_SYSTEM_MAX_STAGES; i++) {
    eos_cmd_stage_t *st = &node->stages[i];
    for (int j = 0; j < st->argc && j < EOS_SYSTEM_MAX_ARGS; j++) {
      free(st->argv[j]);
    }
    free(st->in_file);
    free(st->out_file);
  }
  free(node);
}

typedef struct {
  eos_ast_node_t *node;
} bg_wrapper_t;

static void *bg_thread_entry(void *arg) {
  bg_wrapper_t *bg = (bg_wrapper_t *)arg;
  if (bg) {
    eval_ast(bg->node);
    free_cloned_ast_node(bg->node);
    free(bg);
  }
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

// Compute maximum required stack size across an AST subtree from ebin manifests
static uint32_t ast_required_stack_size(const eos_ast_node_t *node) {
  if (!node)
    return EOS_BIN_DEFAULT_STACK_SIZE;

  uint32_t max_stack = EOS_BIN_DEFAULT_STACK_SIZE;
  if (node->type == EOS_NODE_COMMAND) {
    for (int i = 0; i < node->stage_count && i < EOS_SYSTEM_MAX_STAGES; i++) {
      const eos_cmd_stage_t *st = &node->stages[i];
      if (st->argc <= 0 || !st->argv[0])
        continue;
      if (is_shell_builtin(st->argc, (char **)st->argv))
        continue;

      char interp[EOS_SMALL_STR_LEN];
      char interp_arg[EOS_SMALL_STR_LEN];
      const char *bin_name = st->argv[0];
      if (check_shebang_script(bin_name, interp, sizeof(interp), interp_arg,
                               sizeof(interp_arg))) {
        bin_name = interp;
      }

      eos_bin_t manifest;
      if (load_bin_manifest(bin_name, &manifest) == 0) {
        uint32_t req = eos_bin_effective_stack_size(&manifest);
        if (req > max_stack) {
          max_stack = req;
        }
      }
    }
  } else {
    uint32_t l_stack = ast_required_stack_size(node->left);
    uint32_t r_stack = ast_required_stack_size(node->right);
    if (l_stack > max_stack)
      max_stack = l_stack;
    if (r_stack > max_stack)
      max_stack = r_stack;
  }
  return max_stack;
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

    if (is_shell_builtin(stage_argc, stage_argv)) {
      exec_stages[i].is_builtin = true;
      exec_stages[i].manifest.magic = EOS_BIN_MAGIC;
      exec_stages[i].manifest.flags = EOS_BIN_FLAG_HIDDEN;
      exec_stages[i].manifest.stack_size = EOS_BIN_DEFAULT_STACK_SIZE;
      exec_stages[i].manifest.entry_point = builtin_env_entry;
    } else {
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
    }

    exec_stages[i].argc = stage_argc;
    exec_stages[i].argv = stage_argv;
    exec_stages[i].pipe_in_fd = -1;
    exec_stages[i].pipe_out_fd = -1;
    exec_stages[i].in_file = node->stages[i].in_file;
    exec_stages[i].out_file = node->stages[i].out_file;
    exec_stages[i].out_append = node->stages[i].out_append;
  }

  int ret = 0;

  if (stage_count == 1) {
    eos_tctx_t *tctx = eos_tctx_get();
    eos_tctx_fd_snapshot_t fd_snap = eos_tctx_snapshot_fds(tctx);
    int saved_map[16];
    for (int i = 0; i < 16; i++) {
      saved_map[i] = tctx ? tctx->fd_map[i] : -1;
    }

    fflush(stdout);

    bool redir = has_stage_redirections(&exec_stages[0]);
    int opened_fds[2] = {-1, -1};
    if (apply_stage_redirections(&exec_stages[0], opened_fds) == 0) {
      ret = exec_stages[0].manifest.entry_point(exec_stages[0].argc,
                                                exec_stages[0].argv);
    }

    fflush(stdout);
    clearerr(stdin);
    clearerr(stdout);

    if (redir) {
      if (tctx) {
        for (int i = 0; i < 16; i++)
          tctx->fd_map[i] = saved_map[i];
      }

      close_stage_fds(opened_fds, -1, -1);
    }

    // Clean up any leaked file descriptors or directory handles from the executed command
    eos_tctx_cleanup_leaked_fds(tctx, &fd_snap);
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
      pthread_attr_setstacksize(
          &attr, eos_bin_effective_stack_size(&exec_stages[i].manifest));

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
      if (node->left &&
          (node->left->type != EOS_NODE_COMMAND || node->left->stage_count > 0)) {
        uint32_t bg_stack = ast_required_stack_size(node->left);
        bg_wrapper_t *bg = malloc(sizeof(bg_wrapper_t));
        if (bg) {
          if (tctx) {
            eos_tctx_unreg_memblock(bg, tctx);
          }
          bg->node = clone_ast_node(node->left, tctx);
          if (bg->node) {
            pthread_t t;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_attr_setstacksize(&attr, bg_stack);
            if (pthread_create(&t, &attr, bg_thread_entry, bg) != 0) {
              free_cloned_ast_node(bg->node);
              free(bg);
            }
            pthread_attr_destroy(&attr);
          } else {
            free(bg);
          }
        }
      }
      if (node->right &&
          (node->right->type != EOS_NODE_COMMAND || node->right->stage_count > 0)) {
        status = eval_ast(node->right);
      } else {
        status = 0;
      }
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
  for (int i = 0; i < node->glob_string_count; i++) {
    if (node->glob_strings[i]) {
      free(node->glob_strings[i]);
    }
  }
  free(node);
}

int __wrap_system(const char *cmdline) {
  if (!cmdline) {
    EOS_LOGE("eos_system() called with null cmdline");
    return -1;
  }

  eos_tctx_t *tctx = eos_tctx_get();
  eos_tctx_fd_snapshot_t snap = eos_tctx_snapshot_fds(tctx);

  size_t cmd_len = strlen(cmdline);
  size_t arena_size = cmd_len * 2 + EOS_LARGE_STR_LEN;
  char *arena_buf = malloc(arena_size);
  if (!arena_buf) {
    eos_tctx_cleanup_leaked_fds(tctx, &snap);
    return -1;
  }
  const char *arena_end = arena_buf + arena_size - 1;

  int max_tokens = 128;
  eos_token_t *tokens = calloc(max_tokens, sizeof(eos_token_t));
  if (!tokens) {
    free(arena_buf);
    eos_tctx_cleanup_leaked_fds(tctx, &snap);
    return -1;
  }

  char *cursor = (char *)cmdline;
  char *arena = arena_buf;
  bool is_op = false;
  bool has_wildcard = false;
  char *tok = NULL;
  int num_tokens = 0;

  while ((tok = next_shell_token(&cursor, &arena, arena_end, &is_op, &has_wildcard)) != NULL) {
    if (num_tokens >= max_tokens - 1) {
      break;
    }
    tokens[num_tokens].text = tok;
    tokens[num_tokens].is_op = is_op;
    tokens[num_tokens].has_wildcard = has_wildcard;
    num_tokens++;
  }

  if (num_tokens == 0) {
    free(tokens);
    free(arena_buf);
    eos_tctx_cleanup_leaked_fds(tctx, &snap);
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

  // Perform leak check: verify and close any descriptors left unclosed by commands
  eos_tctx_cleanup_leaked_fds(tctx, &snap);
  return ret;
}

int eos_system(const char *cmdline) {
  return __wrap_system(cmdline);
}
