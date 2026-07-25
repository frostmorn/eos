#include "app.h"
#include "misc/fancymacro.h"
#include <limits.h>
#include <stdio.h>

int eos_native_app_entry_point_empty(int argc, char **argv) {
  EOS_LOGW("Call to not implemented entry_point for an application. ARGC = "
           "%d\nARGV:",
           argc);
  for (int i = 0; i < argc; i++) {
    EOS_LOGW("[%d]%s", i, argv[i]);
  }

  return -1;
}

// ── Tokenizer — handles quotes and backslash escapes ──────────

static char *next_token(char **p) {
  char *src = *p;

  while (*src == ' ')
    src++;
  if (!*src) {
    *p = src;
    return NULL;
  }

  char *dst = src;
  char *token_start = src;
  char in_quote = 0;

  while (*src) {
    if (*src == '\\' && *(src + 1)) {
      src++;
      *dst++ = *src++;
    } else if (!in_quote && (*src == '"' || *src == '\'')) {
      in_quote = *src++;
    } else if (in_quote && *src == in_quote) {
      in_quote = 0;
      src++;
    } else if (!in_quote && *src == ' ') {
      break;
    } else {
      *dst++ = *src++;
    }
  }

  *dst = '\0';
  *p = src;
  return token_start;
}

// ── Path resolution ───────────────────────────────────────────

static void resolve_path(const char *name, char *out, size_t len) {
  if (name[0] == '/') {
    strlcpy(out, name, len);
  } else {
    snprintf(out, len, "/bin/%s", name);
  }
}

// ── eos_system ────────────────────────────────────────────────
int eos_system(const char *cmdline) {

  // TODO: pipeline
  // TODO: cmd detach
  // TODO: redirect outputs

  if (!cmdline) {
    EOS_LOGE("eos_system() called with null cmdline");
    return -1;
  }

  // copy cmdline — tokenizer mutates in-place
  char *buf = strdup(cmdline);
  if (!buf) {
    EOS_LOGE("eos_system() out of memory");
    return -1;
  }

  // max args bounded by cmdline length
  int max_args = strlen(cmdline) / 2 + 2;
  char **argv = malloc(max_args * sizeof(char *));
  if (!argv) {
    EOS_LOGE("eos_system() out of memory");
    free(buf);
    return -1;
  }

  // build argv
  int argc = 0;
  char *p = buf;
  char *tok;
  while ((tok = next_token(&p)) != NULL && argc < max_args)
    argv[argc++] = tok;
  argv[argc] = NULL;

  if (argc == 0) {
    free(argv);
    free(buf);
    return 0;
  }

  // resolve path
  char path[PATH_MAX];
  resolve_path(argv[0], path, sizeof(path));

  // open file
  FILE *f = fopen(path, "r");
  if (!f) {
    EOS_LOGE("eos_system() not found: %s", path);
    free(argv);
    free(buf);
    return -1;
  }

  // read magic
  uint32_t magic = 0;
  fread(&magic, sizeof(magic), 1, f);

  // native app — read full manifest from file, call entry_point directly
  if (magic == EOS_NATIVE_APP_MAGIC) {
    rewind(f);
    eos_native_app_manifest_t manifest;
    fread(&manifest, sizeof(manifest), 1, f);
    fclose(f);

    EOS_LOGI("eos_system() launching: %s", manifest.name);
    int ret = manifest.entry_point(argc, argv);
    free(argv);
    free(buf);
    return ret;
  }

  // shebang script
  rewind(f);
  char shebang[EOS_SMALL_STR_LEN] = {0};
  fgets(shebang, sizeof(shebang), f);
  fclose(f);

  if (shebang[0] == '#' && shebang[1] == '!') {
    char *interp = shebang + 2;
    interp[strcspn(interp, "\n\r")] = '\0';
    while (*interp == ' ')
      interp++;

    char interp_cmd[PATH_MAX];
    snprintf(interp_cmd, sizeof(interp_cmd), "%s %s", interp, cmdline);

    EOS_LOGI("eos_system() shebang: %s -> %s", path, interp);
    free(argv);
    free(buf);
    return eos_system(interp_cmd);
  }

  EOS_LOGE("eos_system() unknown format: %s", path);
  free(argv);
  free(buf);
  return -1;
}