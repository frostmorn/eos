#include "ecore/fapi.h"
#include "ecore/log.h"
#include "ecore/proc.h"
#include "ecore/vfs.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <reent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

extern bool eos_console_is_connected(void);

bool __wrap_usb_serial_jtag_is_connected(void) {
  return true;
}

int __wrap__open_r(struct _reent *r, char *path, int flags, int mode) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  int fd = eos_vfs_open_r(r, _path, flags, mode);

  if (fd >= 0) {
    eos_proc_t *proc = eos_proc_current();
    if (proc && fd < FD_SETSIZE) {
      eos_proc_fd_map_set(proc, fd, -1);
    }
    eos_proc_reg_fd(fd);
  }

  eos_proc_leave_kernel(prev_k);
  return fd;
}

int __wrap__close_r(struct _reent *r, int fd) {
  bool prev_k = eos_proc_enter_kernel();
  eos_proc_t *proc = eos_proc_current();

  // If closing a virtual redirected descriptor (e.g. dup2 target),
  // clear its mapping without closing the underlying target unless owned.
  if (proc && fd >= 0 && fd < FD_SETSIZE && eos_proc_fd_map_get(proc, fd) >= 0) {
    eos_proc_fd_map_set(proc, fd, -1);
    eos_proc_leave_kernel(prev_k);
    return 0;
  }

  if (fd >= 0 && proc) {
    pthread_mutex_lock(&proc->fd_mutex);
    for (size_t i = 0; i < kv_size(proc->fd_map); i++) {
      if (kv_A(proc->fd_map, i) == fd) {
        kv_A(proc->fd_map, i) = -1;
      }
    }
    bool prev_h = eos_proc_heap_tracking_disable();
    for (size_t i = 0; i < kv_size(proc->fds); i++) {
      if (kv_A(proc->fds, i) == fd) {
        kv_drop_fast(int, proc->fds, i);
        break;
      }
    }
    eos_proc_heap_tracking_restore(prev_h);
    pthread_mutex_unlock(&proc->fd_mutex);
  }

  int ret = eos_vfs_close_r(r, fd);

  eos_proc_leave_kernel(prev_k);
  return ret;
}

static int resolve_read_fd(int fd) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return fd;
  if (fd == STDIN_FILENO || (stdin && fd == fileno(stdin))) {
    int m = eos_proc_fd_map_get(proc, STDIN_FILENO);
    return (m >= 0) ? m : fd;
  }
  return eos_proc_resolve_fd(fd);
}

static int resolve_write_fd(int fd) {
  // Never redirect writes on STDIN_FILENO (fd 0 is also used internally by ESP-IDF vfs_console.fd_primary)
  if (fd == STDIN_FILENO) {
    return fd;
  }
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return fd;
  if (fd == STDOUT_FILENO || (stdout && fd == fileno(stdout))) {
    int m = eos_proc_fd_map_get(proc, STDOUT_FILENO);
    if (m >= 0) {
      return m;
    }
    return fd;
  }
  if (fd == STDERR_FILENO || (stderr && fd == fileno(stderr))) {
    int m = eos_proc_fd_map_get(proc, STDERR_FILENO);
    if (m >= 0) {
      return m;
    }
    return fd;
  }
  return eos_proc_resolve_fd(fd);
}

ssize_t __wrap__read_r(struct _reent *r, int fd, void *dst, size_t size) {
  bool prev_k = eos_proc_enter_kernel();
  int real_fd = resolve_read_fd(fd);
  if (real_fd == STDIN_FILENO && !eos_console_is_connected()) {
    errno = EAGAIN;
    eos_proc_leave_kernel(prev_k);
    return -1;
  }

  ssize_t ret = eos_vfs_read_r(r, real_fd, dst, size);

  eos_proc_leave_kernel(prev_k);
  return ret;
}

ssize_t __wrap__write_r(struct _reent *r, int fd, void *buf, size_t size) {
  bool prev_k = eos_proc_enter_kernel();
  int real_fd = resolve_write_fd(fd);
  ssize_t ret = eos_vfs_write_r(r, real_fd, buf, size);

  eos_proc_leave_kernel(prev_k);
  return ret;
}

off_t __wrap__lseek_r(struct _reent *r, int fd, off_t size, int mode) {
  bool prev_k = eos_proc_enter_kernel();
  int real_fd = fd;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    if (fd == STDIN_FILENO || (stdin && fd == fileno(stdin))) {
      int m = eos_proc_fd_map_get(proc, STDIN_FILENO);
      real_fd = (m >= 0) ? m : fd;
    } else if (fd == STDOUT_FILENO || (stdout && fd == fileno(stdout)) ||
               fd == STDERR_FILENO || (stderr && fd == fileno(stderr))) {
      real_fd = resolve_write_fd(fd);
    } else {
      real_fd = eos_proc_resolve_fd(fd);
    }
  }

  off_t ret = eos_vfs_lseek_r(r, real_fd, size, mode);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int __wrap__fstat_r(struct _reent *r, int fd, struct stat *st) {
  bool prev_k = eos_proc_enter_kernel();
  int real_fd = fd;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    if (fd == STDIN_FILENO || (stdin && fd == fileno(stdin))) {
      int m = eos_proc_fd_map_get(proc, STDIN_FILENO);
      real_fd = (m >= 0) ? m : fd;
    } else if (fd == STDOUT_FILENO || (stdout && fd == fileno(stdout)) ||
               fd == STDERR_FILENO || (stderr && fd == fileno(stderr))) {
      real_fd = resolve_write_fd(fd);
    } else {
      real_fd = eos_proc_resolve_fd(fd);
    }
  }

  int ret = eos_vfs_fstat_r(r, real_fd, st);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int dup2(int oldfd, int newfd) {
  if (oldfd < 0 || oldfd >= FD_SETSIZE || newfd < 0 || newfd >= FD_SETSIZE) {
    errno = EBADF;
    return -1;
  }

  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    errno = EBADF;
    return -1;
  }

  int real_old = eos_proc_resolve_fd(oldfd);
  if (newfd == real_old) {
    eos_proc_fd_map_set(proc, newfd, -1);
  } else {
    eos_proc_fd_map_set(proc, newfd, real_old);
  }

  return newfd;
}

int dup(int oldfd) {
  if (oldfd < 0 || oldfd >= FD_SETSIZE) {
    errno = EBADF;
    return -1;
  }

  eos_proc_t *proc = eos_proc_current();
  if (!proc) {
    errno = EBADF;
    return -1;
  }

  int real_old = eos_proc_resolve_fd(oldfd);

  pthread_mutex_lock(&proc->fd_mutex);
  for (int vfd = FD_SETSIZE - 1; vfd >= 3; vfd--) {
    if ((size_t)vfd < kv_size(proc->fd_map) && kv_A(proc->fd_map, vfd) >= 0)
      continue;

    bool in_use = false;
    for (size_t i = 0; i < kv_size(proc->fds); i++) {
      if (kv_A(proc->fds, i) == vfd) {
        in_use = true;
        break;
      }
    }
    if (!in_use) {
      bool prev_h = eos_proc_heap_tracking_disable();
      while (kv_size(proc->fd_map) <= (size_t)vfd) {
        kv_push(int, proc->fd_map, -1);
      }
      kv_A(proc->fd_map, vfd) = real_old;
      eos_proc_heap_tracking_restore(prev_h);
      pthread_mutex_unlock(&proc->fd_mutex);
      return vfd;
    }
  }
  pthread_mutex_unlock(&proc->fd_mutex);

  errno = EMFILE;
  return -1;
}

int __wrap__stat_r(struct _reent *r, char *path, struct stat *st) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  int ret = eos_vfs_stat_r(r, _path, st);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int __wrap__link_r(struct _reent *r, char *n1, char *n2) {
  bool prev_k = eos_proc_enter_kernel();
  char b1[PATH_MAX], b2[PATH_MAX];
  char *_n1 = eos_fapi_path_resolve(n1, b1);
  char *_n2 = eos_fapi_path_resolve(n2, b2);

  int ret = eos_vfs_link_r(r, _n1, _n2);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int __wrap__unlink_r(struct _reent *r, char *path) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  int ret = eos_vfs_unlink_r(r, _path);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int __wrap__rename_r(struct _reent *r, char *src, char *dst) {
  bool prev_k = eos_proc_enter_kernel();
  char b1[PATH_MAX], b2[PATH_MAX];
  char *_src = eos_fapi_path_resolve(src, b1);
  char *_dst = eos_fapi_path_resolve(dst, b2);

  int ret = eos_vfs_rename_r(r, _src, _dst);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

DIR *__wrap_opendir(char *name) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_name = eos_fapi_path_resolve(name, buf);

  DIR *pdir = eos_vfs_opendir(_name);
  if (pdir) {
    eos_proc_reg_dir(pdir);
  }

  eos_proc_leave_kernel(prev_k);
  return pdir;
}

struct dirent *__wrap_readdir(DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return NULL;
  }

  bool prev_k = eos_proc_enter_kernel();
  struct dirent *ent = eos_vfs_readdir(pdir);
  eos_proc_leave_kernel(prev_k);
  return ent;
}

int __wrap_closedir(DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return -1;
  }

  bool prev_k = eos_proc_enter_kernel();
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    eos_proc_unreg_dir(pdir);
  }

  int ret = eos_vfs_closedir(pdir);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

void __wrap_seekdir(DIR *pdir, long loc) {
  bool prev_k = eos_proc_enter_kernel();
  eos_vfs_seekdir(pdir, loc);
  eos_proc_leave_kernel(prev_k);
}

long __wrap_telldir(DIR *pdir) {
  bool prev_k = eos_proc_enter_kernel();
  long ret = eos_vfs_telldir(pdir);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

void __wrap_rewinddir(DIR *pdir) {
  bool prev_k = eos_proc_enter_kernel();
  eos_vfs_rewinddir(pdir);
  eos_proc_leave_kernel(prev_k);
}

int __wrap_mkdir(char *path, mode_t mode) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  int ret = eos_vfs_mkdir(_path, mode);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int __wrap_rmdir(const char *path) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve((char *)path, buf);

  int ret = eos_vfs_rmdir(_path);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int __wrap_access(const char *path, int amode) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve((char *)path, buf);

  int ret = eos_vfs_access(_path, amode);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int __wrap_ioctl(int fd, int request, ...) {
  bool prev_k = eos_proc_enter_kernel();
  int real_fd = fd;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    real_fd = eos_proc_resolve_fd(fd);
  }

  va_list args;
  va_start(args, request);
  int ret = eos_vfs_ioctl(real_fd, request, args);
  va_end(args);

  eos_proc_leave_kernel(prev_k);
  return ret;
}

int symlink(const char *target, const char *linkpath) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_linkpath = eos_fapi_path_resolve((char *)linkpath, buf);

  int ret = eos_vfs_symlink(target, _linkpath);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
  bool prev_k = eos_proc_enter_kernel();
  char pbuf[PATH_MAX];
  char *_path = eos_fapi_path_resolve((char *)path, pbuf);

  ssize_t ret = eos_vfs_readlink(_path, buf, bufsiz);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

int lstat(const char *path, struct stat *st) {
  bool prev_k = eos_proc_enter_kernel();
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve((char *)path, buf);

  int ret = eos_vfs_lstat(_path, st);
  eos_proc_leave_kernel(prev_k);
  return ret;
}
