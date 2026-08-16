#include "ecore/appctx.h"
#include "emisc/fancymacro.h"
#include <dirent.h>
#include <reent.h>
#include <string.h>
#include "ecore/fapi.h"


// (^_^)==\~ WRAPPERS

// most of magic is hidden in flags passed to linker inside CMakeLists.txt
// allowing us to wrap around almost everything we want. Here we do use it
// for solving path traversal problem

// syscalls:

extern int __real__open_r(struct _reent *r, char *path, int flags, int mode);
int __wrap__open_r(struct _reent *r, char *path, int flags, int mode) {
  //  EOS_LOGI("_r_open_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__open_r(r, _path, flags, mode);
}

extern int __real__stat_r(struct _reent *r, char *path, struct stat *st);
int __wrap__stat_r(struct _reent *r, char *path, struct stat *st) {
  //  EOS_LOGI("_r_stat_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__stat_r(r, _path, st);
}

extern int __real__link_r(struct _reent *r, char *n1, char *n2);
int __wrap__link_r(struct _reent *r, char *n1, char *n2) {
  //  EOS_LOGI("_r_link_r: n1=%s n2=%s\n", n1, n2);

  char *_n1 = eos_fapi_get_buffer(0);
  _n1 = eos_fapi_path_resolve(n1, _n1);

  char *_n2 = eos_fapi_get_buffer(1);
  _n2 = eos_fapi_path_resolve(n2, _n2);

  return __real__link_r(r, _n1, _n2);
}

extern int __real__unlink_r(struct _reent *r, char *path);
int __wrap__unlink_r(struct _reent *r, char *path) {
  //  EOS_LOGI("_r_unlink_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__unlink_r(r, _path);
}

extern int __real__rename_r(struct _reent *r, char *src, char *dst);
int __wrap__rename_r(struct _reent *r, char *src, char *dst) {
  //  EOS_LOGI("_r_rename_r: src=%s dst=%s\n", src, dst);

  char *_src = eos_fapi_get_buffer(0);
  _src = eos_fapi_path_resolve(src, _src);

  char *_dst = eos_fapi_get_buffer(1);
  _dst = eos_fapi_path_resolve(dst, _dst);

  return __real__rename_r(r, _src, _dst);
}

// also known as esp_vfs_*
extern DIR *__real_opendir(char *name);

DIR *__wrap_opendir(char *name) {
  //  EOS_LOGI("opendir: name=%s\n", name);

  char *_name = eos_fapi_get_buffer(0);
  _name = eos_fapi_path_resolve(name, _name);

  return __real_opendir(_name);
}

extern int __real_mkdir(char *path, mode_t mode);
int __wrap_mkdir(char *path, mode_t mode) {
  //  EOS_LOGI("mkdir: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real_mkdir(_path, mode);
}
// [-_-)==|~ END_WRAPPERS
