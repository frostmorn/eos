#include "ecore/fapi.h"
#include "ecore/log.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include "pregen.h"
#include <dirent.h>
#include <errno.h>
#include <reent.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
// (^_^)==\~ WRAPPERS

// most of magic is hidden in flags passed to linker inside CMakeLists.txt
// allowing us to wrap around almost everything we want. Here we do use it
// for solving path traversal problem and per-thread file descriptor redirection

// Call definitions provided in components/vfs/vfs_calls.c

// libc syscalls:

extern int __real__open_r(struct _reent *r, char *path, int flags, int mode);
extern int __real__close_r(struct _reent *r, int fd);
extern ssize_t __real__read_r(struct _reent *r, int fd, void *dst, size_t size);
extern ssize_t __real__write_r(struct _reent *r, int fd, const void *data, size_t size);
extern off_t __real__lseek_r(struct _reent *r, int fd, off_t size, int mode);
extern int __real__fstat_r(struct _reent *r, int fd, struct stat *st);
extern int __real__stat_r(struct _reent *r, char *path, struct stat *st);
extern int __real__link_r(struct _reent *r, char *n1, char *n2);
extern int __real__unlink_r(struct _reent *r, char *path);
extern int __real__rename_r(struct _reent *r, char *src, char *dst);

// non syscalls //?
extern DIR *__real_opendir(char *name);
extern int __real_closedir(DIR *pdir);
extern int __real_mkdir(char *path, mode_t mode);

int __wrap__open_r(struct _reent *r, char *path, int flags, int mode) {
  //  EOS_LOGI("_r_open_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);
  int fd = __real__open_r(r, _path, flags, mode);

  if (fd >= 0) {
    eos_tctx_t *tctx = eos_tctx_get();
    if (tctx && fd < FD_SETSIZE) {
      tctx->fd_map[fd] = -1;
    }
    eos_tctx_reg_fd(fd, tctx);
  }

  return fd;
}

int __wrap__close_r(struct _reent *r, int fd) {
  eos_tctx_t *tctx = eos_tctx_get();

  // If closing a virtual redirected descriptor (e.g. dup2 target),
  // clear its mapping without closing the underlying target unless owned.
  if (tctx && fd >= 0 && fd < FD_SETSIZE && tctx->fd_map[fd] >= 0) {
    tctx->fd_map[fd] = -1;
    return 0;
  }

  int ret = __real__close_r(r, fd);

  if (ret == 0 && tctx) {
    for (int i = 0; i < FD_SETSIZE; i++) {
      if (tctx->fd_map[i] == fd) {
        tctx->fd_map[i] = -1;
      }
    }
    eos_tctx_unreg_fd(fd, tctx);
  }

  return ret;
}

ssize_t __wrap__read_r(struct _reent *r, int fd, void *dst, size_t size) {
  int real_fd = eos_tctx_resolve_fd(fd);
  if (real_fd == STDIN_FILENO && !eos_console_is_connected()) {
    errno = EAGAIN;
    return -1;
  }
  return __real__read_r(r, real_fd, dst, size);
}

ssize_t __wrap__write_r(struct _reent *r, int fd, const void *data, size_t size) {
  int real_fd = eos_tctx_resolve_fd(fd);
  if ((real_fd == STDOUT_FILENO || real_fd == STDERR_FILENO) &&
      !eos_console_is_connected()) {
    return (ssize_t)size;
  }
  return __real__write_r(r, real_fd, data, size);
}

off_t __wrap__lseek_r(struct _reent *r, int fd, off_t size, int mode) {
  return __real__lseek_r(r, eos_tctx_resolve_fd(fd), size, mode);
}

int __wrap__fstat_r(struct _reent *r, int fd, struct stat *st) {
  return __real__fstat_r(r, eos_tctx_resolve_fd(fd), st);
}

int dup2(int oldfd, int newfd) {
  if (oldfd < 0 || oldfd >= FD_SETSIZE || newfd < 0 || newfd >= FD_SETSIZE) {
    errno = EBADF;
    return -1;
  }

  eos_tctx_t *tctx = eos_tctx_get();
  if (!tctx) {
    errno = EBADF;
    return -1;
  }

  int real_old = eos_tctx_resolve_fd(oldfd);
  if (newfd == real_old) {
    tctx->fd_map[newfd] = -1;
  } else {
    tctx->fd_map[newfd] = real_old;
  }

  return newfd;
}

int dup(int oldfd) {
  if (oldfd < 0 || oldfd >= FD_SETSIZE) {
    errno = EBADF;
    return -1;
  }

  eos_tctx_t *tctx = eos_tctx_get();
  if (!tctx) {
    errno = EBADF;
    return -1;
  }

  int real_old = eos_tctx_resolve_fd(oldfd);

  // Allocate a virtual descriptor slot from the top down to avoid
  // colliding with ESP-IDF VFS descriptor numbers.
  for (int vfd = FD_SETSIZE - 1; vfd >= 3; vfd--) {
    if (tctx->fd_map[vfd] >= 0)
      continue;

    bool in_use = false;
    for (size_t i = 0; i < kv_size(tctx->fds); i++) {
      if (kv_A(tctx->fds, i) == vfd) {
        in_use = true;
        break;
      }
    }
    if (!in_use) {
      tctx->fd_map[vfd] = real_old;
      return vfd;
    }
  }

  errno = EMFILE;
  return -1;
}

int __wrap__stat_r(struct _reent *r, char *path, struct stat *st) {
  //  EOS_LOGI("_r_stat_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__stat_r(r, _path, st);
}

int __wrap__link_r(struct _reent *r, char *n1, char *n2) {
  //  EOS_LOGI("_r_link_r: n1=%s n2=%s\n", n1, n2);

  char *_n1 = eos_fapi_get_buffer(0);
  _n1 = eos_fapi_path_resolve(n1, _n1);

  char *_n2 = eos_fapi_get_buffer(1);
  _n2 = eos_fapi_path_resolve(n2, _n2);

  return __real__link_r(r, _n1, _n2);
}

int __wrap__unlink_r(struct _reent *r, char *path) {
  //  EOS_LOGI("_r_unlink_r: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__unlink_r(r, _path);
}

int __wrap__rename_r(struct _reent *r, char *src, char *dst) {
  //  EOS_LOGI("_r_rename_r: src=%s dst=%s\n", src, dst);

  char *_src = eos_fapi_get_buffer(0);
  _src = eos_fapi_path_resolve(src, _src);

  char *_dst = eos_fapi_get_buffer(1);
  _dst = eos_fapi_path_resolve(dst, _dst);

  return __real__rename_r(r, _src, _dst);
}

// also known as esp_vfs_*

DIR *__wrap_opendir(char *name) {
  //  EOS_LOGI("opendir: name=%s\n", name);

  char *_name = eos_fapi_get_buffer(0);
  _name = eos_fapi_path_resolve(name, _name);
  
  DIR *pdir = __real_opendir(_name);
  
  if (pdir)
    eos_tctx_reg_dir(pdir, eos_tctx_get());

  return pdir;
}

int __wrap_closedir(DIR *pdir){
  int ret = __real_closedir(pdir);

  if (ret == 0)
    eos_tctx_unreg_dir(pdir, eos_tctx_get());

  return ret;
}

int __wrap_mkdir(char *path, mode_t mode) {
  //  EOS_LOGI("mkdir: path=%s\n", path);

  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real_mkdir(_path, mode);
}
// [-_-)==|~ END_WRAPPERS
