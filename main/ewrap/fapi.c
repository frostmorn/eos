#include <reent.h>
#include <limits.h>
#include "emisc/fancymacro.h"
#include "ecore/appctx.h"
#include <dirent.h>
#include <esp_cpu.h>

// For file path manipulation we always need two buffers since some calls 
// wrapped here require two path arguments
// Of course we can store it somewhere on application context, but that's
// can easily become very heavy, especially at moment when we start to 
// use actual multithreading

#define EOS_FAPI_COUNT_BUFFERS 2

char eos_fapi_buffer[SOC_CPU_CORES_NUM*EOS_FAPI_COUNT_BUFFERS][PATH_MAX];

char *eos_fapi_get_buffer(size_t index){

  if (index > EOS_FAPI_COUNT_BUFFERS)
  {
    EOS_LOGE("eos_fapi_get_buffer: trying to use inacessible buffer %d\n", index);
    // of course we can return null, but imagine amount of situations we had 
    // to handle, we already fucked up since it used wrong way
    abort();
  }

  uint8_t core = esp_cpu_get_core_id();

  return eos_fapi_buffer[index * core];
}

// Actual path handling happens here
static inline char* eos_fapi_path_resolve(char *path, char *buffer){
  if (!path) return path;

//  EOS_LOGE("eos_fapi_path_resolve: path=%s\n", path);

  // Absolute path
  if (path[0] == '/') return path;

  // Relative path
  eos_app_ctx_t *ctx = eos_get_current_app_ctx();

  strcpy(buffer, ctx->cwd);
  if (ctx->cwd[strlen(ctx->cwd)-1] != '/')
    strcat(buffer, "/");
  strcat(buffer, path);

  // TODO: Handle . and ..

//  EOS_LOGE("eos_fapi_path_resolve new_path=%s", out);  

  return buffer;
}

// (^_^)==\~ WRAPPERS

// most of magic is hidden in flags passed to linker inside CMakeLists.txt
// allowing us to wrap around almost everything we want. Here we do use it
// for solving path traversal problem

// syscalls:

extern int __real__open_r(struct _reent *r, const char * path, int flags, int mode);
int __wrap__open_r(struct _reent *r, const char * path, int flags, int mode){
//  EOS_LOGI("_r_open_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);
 
  return __real__open_r(r, _path, flags, mode);
}

extern int __real__stat_r(struct _reent *r, const char * path, struct stat * st);
int __wrap__stat_r(struct _reent *r, const char * path, struct stat * st){
//  EOS_LOGI("_r_stat_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__stat_r(r, _path, st);
}

extern int __real__link_r(struct _reent *r, const char* n1, const char* n2);
int __wrap__link_r(struct _reent *r, const char* n1, const char* n2){
//  EOS_LOGI("_r_link_r: n1=%s n2=%s\n", n1, n2);

  char *_n1 = eos_fapi_get_buffer(0);
  _n1 = eos_fapi_path_resolve(n1, _n1);

  char *_n2 = eos_fapi_get_buffer(1);
  _n2 = eos_fapi_path_resolve(n2, _n2);

  return __real__link_r(r, _n1, _n2);
}

extern int __real__unlink_r(struct _reent *r, const char *path);
int __wrap__unlink_r(struct _reent *r, const char *path){
//  EOS_LOGI("_r_unlink_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);
  
  return __real__unlink_r(r, _path);
}

extern int __real__rename_r(struct _reent *r, const char *src, const char *dst);
int __wrap__rename_r(struct _reent *r, const char *src, const char *dst){
//  EOS_LOGI("_r_rename_r: src=%s dst=%s\n", src, dst);

  char *_src = eos_fapi_get_buffer(0);
  _src = eos_fapi_path_resolve(src, _src);

  char *_dst = eos_fapi_get_buffer(1);
  _dst = eos_fapi_path_resolve(dst, _dst);

  return __real__rename_r(r, _src, _dst);
}

// also known as esp_vfs_*
extern DIR* __real_opendir(const char *name);

DIR* __wrap_opendir(const char *name){
//  EOS_LOGI("opendir: name=%s\n", name);

  char *_name = eos_fapi_get_buffer(0);
  _name = eos_fapi_path_resolve(name, _name);

  return __real_opendir(_name);
}

extern int __real_mkdir(const char*path, mode_t mode);
int __wrap_mkdir(const char *path, mode_t mode){
//  EOS_LOGI("mkdir: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real_mkdir(_path, mode);
}
// [-_-)==|~ END_WRAPPERS
