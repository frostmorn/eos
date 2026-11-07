#include "ecore/fapi.h"
#include "ecore/proc.h"
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <stddef.h>

extern char *__real_getcwd(char *buf, size_t size);
extern int _real_chdir(const char *path);

char *__wrap_getcwd(char *buf, size_t size){
  eos_proc_t *proc = eos_proc_current();
  const char *cwd = proc ? proc->cwd : "/";

  if (buf == NULL){
    return strdup(cwd);
  }
  strlcpy(buf, cwd, size);
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
  if (!dir)
  {
    errno = ENOTDIR;
    return -1;
  }
 
  closedir(dir);

  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    strlcpy(proc->cwd, _path, sizeof(proc->cwd));
    eos_proc_set_cwd(proc->pid, _path);
  }
   
  return 0;
}
