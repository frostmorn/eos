#include "fapi.h"
#include <string.h>
#include <soc/soc_caps.h>
#include <esp_cpu.h>
#include <limits.h>
#include "emisc/fancymacro.h"
#include "ecore/appctx.h"

// For file path manipulation we always need two buffers since some calls
// wrapped here require two path arguments
// Of course we can store it somewhere on application context, but that's
// can easily become very heavy, especially at moment when we start to
// use actual multithreading


#define EOS_FAPI_COUNT_BUFFERS 2

char eos_fapi_buffer[SOC_CPU_CORES_NUM * EOS_FAPI_COUNT_BUFFERS][PATH_MAX];

char *eos_fapi_get_buffer(size_t index) {

  if (index > EOS_FAPI_COUNT_BUFFERS) {
    EOS_LOGE("eos_fapi_get_buffer: trying to use inacessible buffer %d\n",
             index);
    // of course we can return null, but imagine amount of situations we had
    // to handle, we already fucked up since it used wrong way
    abort();
  }

  uint8_t core = esp_cpu_get_core_id() + 1; // yeah, it starts from zero

  return eos_fapi_buffer[index * core];
}

// Collapses "." and ".." segments out of an absolute path, in place,
// using only the buffer itself as scratch space (a read cursor and a
// write cursor - no side storage). Assumes path[0] == '/'.
//
// The write cursor never runs ahead of the read cursor (output can
// only shrink relative to input), so writing forward into the same
// buffer we're still reading from is safe; ".." backtracks the write
// cursor to the previous '/' already emitted, but never above root.
void eos_fapi_path_normalize(char *path) {
  // Defensive: the whole safety argument for writing forward into this
  // same buffer rests on "output length <= input length". An empty
  // string breaks that (root alone is 2 bytes: '/' + NUL), so refuse to
  // touch a path that doesn't already satisfy the precondition rather
  // than risk writing past a buffer sized for the original (possibly
  // empty) string. Every real call site already guarantees path[0]=='/'.
  if (path[0] != '/')
    return;

  size_t len = strlen(path);
  size_t w = 0;
  path[w++] = '/'; // root

  size_t i = 0;
  while (i < len) {
    while (i < len && path[i] == '/')
      i++;
    if (i >= len)
      break;

    size_t seg_start = i;
    while (i < len && path[i] != '/')
      i++;
    size_t seg_len = i - seg_start;

    if (seg_len == 1 && path[seg_start] == '.') {
      continue; // "." -> current dir, drop it
    }

    if (seg_len == 2 && path[seg_start] == '.' && path[seg_start + 1] == '.') {
      // ".." -> backtrack the write cursor to before the last emitted
      // segment, but never rise above root.
      if (w > 1) {
        size_t j = w - 1;
        while (j > 0 && path[j - 1] != '/')
          j--;
        if (j > 1)
          j--; // also drop the '/' that preceded it
        w = j;
      }
      continue;
    }

    if (w > 1)
      path[w++] = '/';
    memmove(&path[w], &path[seg_start], seg_len);
    w += seg_len;
  }

  path[w] = '\0';
}

// Actual path handling happens here
char *eos_fapi_path_resolve(char *path, char *buffer) {
  if (!path)
    return path;

  // EOS_LOGE("eos_fapi_path_resolve: path=%s\n", path);

  // Absolute path - still needs "." / ".." collapsed (e.g. "/a/../b"),
  // so copy it into our scratch buffer rather than handing the caller's
  // pointer straight through.
  if (path[0] == '/') {
    strcpy(buffer, path);
    eos_fapi_path_normalize(buffer);
    return buffer;
  }

  // Relative path
  eos_app_ctx_t *ctx = eos_get_current_app_ctx();

  strcpy(buffer, ctx->cwd);
  if (ctx->cwd[strlen(ctx->cwd) - 1] != '/')
    strcat(buffer, "/");
  strcat(buffer, path);

  eos_fapi_path_normalize(buffer);

  //  EOS_LOGE("eos_fapi_path_resolve new_path=%s", buffer);

  return buffer;
}

