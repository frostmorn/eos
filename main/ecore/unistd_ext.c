#include "unistd_ext.h"
#include "ecore/fapi.h"
#include "ecore/appctx.h"
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <stddef.h>

char *getcwd_fast(){
  return eos_get_current_app_ctx()->cwd;
}

char *getcwd(char *buf, size_t size){
  eos_app_ctx_t *ctx = eos_get_current_app_ctx();

  if (strlen(ctx->cwd)+1 > size){
    errno = ERANGE;
    return NULL;
  }
  strcpy(buf, ctx->cwd);

  return buf;
}

int chdir(const char*path){
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

  eos_app_ctx_t *ctx = eos_get_current_app_ctx();

  strcpy(ctx->cwd, _path);
   
  return 0;
}
