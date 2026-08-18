#include "ecore/fapi.h"
#include "ecore/appctx.h"
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <stddef.h>
// realpath() seems to be made correctly

extern char *__real_getcwd(char *buf, size_t size);
extern int _real_chdir(const char *path);

char *__wrap_getcwd(char *buf, size_t size){
  eos_app_ctx_t *ctx = eos_app_ctx_get_cur();

  if (buf == NULL){
    return strdup(ctx->cwd);
  }
  strlcpy(buf, ctx->cwd, size);
  return buf;
}

int __wrap_chdir(char*path){
  if (!path){
    errno = ENOENT;
    return -1;
  }

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  DIR *dir = opendir(_path);
  // check if openable
  if (!dir)
  {
    errno = ENOTDIR;
    return -1;
  }
 
  closedir(dir);

  eos_app_ctx_t *ctx = eos_app_ctx_get_cur();

  strcpy(ctx->cwd, _path);
   
  return 0;
}
/*
// Maybe we would like to intercept it, one day or another
int chmod(const char *path, mode_t mode)
{
    return 0;
}

int dirfd(DIR *dirp)
{
    errno = ENOSYS;
    return -1;
}

*/
