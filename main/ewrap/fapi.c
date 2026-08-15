#include <reent.h>
#include "emisc/fancymacro.h"

// We've to wrap arround some calls to implement application context concept

// syscalls:

extern int __real__open_r(struct _reent *r, const char * path, int flags, int mode);
int __wrap__open_r(struct _reent *r, const char * path, int flags, int mode){
  EOS_LOGI("_r_open_r: path=%s\n", path);

  return __real__open_r(r, path, flags, mode);
}


extern int __real__stat_r(struct _reent *r, const char * path, struct stat * st);
int __wrap__stat_r(struct _reent *r, const char * path, struct stat * st){
  EOS_LOGI("_r_stat_r: path=%s\n", path);

  return __real__stat_r(r, path, st);
}

extern int __real__link_r(struct _reent *r, const char* n1, const char* n2);
int __wrap__link_r(struct _reent *r, const char* n1, const char* n2){
  EOS_LOGI("_r_link_r: n1=%s n2=%s\n", n1, n2);

  return __real__link_r(r, n1, n2);
}

extern int __real__unlink_r(struct _reent *r, const char *path);
int __wrap__unlink_r(struct _reent *r, const char *path){
  EOS_LOGI("_r_unlink_r: path=%s\n", path);
  
  return __real__unlink_r(r, path);
}

extern int __real__rename_r(struct _reent *r, const char *src, const char *dst);
int __wrap__rename_r(struct _reent *r, const char *src, const char *dst){
  EOS_LOGI("_r_rename_r: src=%s dst=%s\n", src, dst);

  return __real__rename_r(r, src, dst);
}


