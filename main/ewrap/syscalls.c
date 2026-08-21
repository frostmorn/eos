#include "ecore/binfs.h"
#include "emisc/fancymacro.h"
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

// This call isn't implemented in picolib, but cause it have a declaration
// burried somewhere in <stdlib.h> why not to use it, right?
// components/esp_lib/src/syscalls.c
extern int __real_system(const char *str);

// helpers
static char *skip_ws(char *p) {
  while (*p && isspace((unsigned char)*p))
    p++;
  return *p ? p : NULL;
}

static char *token_end(char *p) {
  while (*p && !isspace((unsigned char)*p))
    p++;
  return p;
}

static void resolve_path(const char *name, char *out, size_t len) {
  if (name[0] == '/') {
    strlcpy(out, name, len);
  } else {
    snprintf(out, len, EOS_BINFS_ROOT "/%s", name);
  }
}

// TODO: make it's behaviour as close as possible to system() from <stdlib.h>
int __wrap_system(const char *cmdline) {

  // ALL OF features mentioned below require an actual AST
  // TODO: pipeline
  // TODO: cmd detach
  // TODO: redirect outputs

  // resolve path
  char path[EOS_MID_STR_LEN];

  if (!cmdline) {
    EOS_LOGE("eos_system() called with null cmdline");
    return -1;
  }
  // Copy command line
  char *buf = strdup(cmdline);
  if (!buf) {
    EOS_LOGE("eos_system() out of memory");
    return -1;
  }

  // count
  int argc = 0;
  char *p = skip_ws(buf);
  while (p) {
    argc++;
    char *end = token_end(p);
    p = *end ? skip_ws(end) : NULL;
  }

  // build
  char **argv = calloc(argc + 1, sizeof(*argv));
  argc = 0;
  p = skip_ws(buf);
  while (p) {
    char *end = token_end(p);
    argv[argc++] = p;
    if (*end) {
      *end = '\0';
      p = skip_ws(end + 1);
    } else
      p = NULL;
  }
  argv[argc] = NULL;

  if (argc == 0) {
    free(argv);
    free(buf);
    return 0;
  }

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

    // EOS_LOGI("eos_system() launching: %s", manifest.name);
    int ret = manifest.entry_point(argc, argv);
    free(argv);
    free(buf);
    return ret;
  }
  /*
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
      */

  EOS_LOGE("eos_system() unknown format: %s", path);
  free(argv);
  free(buf);
  return -1;
}
