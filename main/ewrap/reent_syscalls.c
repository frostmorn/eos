#include "ecore/fapi.h"
#include "ecore/log.h"
#include "ecore/proc.h"
#include "ecore/vfs.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <reent.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

extern bool eos_console_is_connected(void);

bool __wrap_usb_serial_jtag_is_connected(void) {
  return true;
}

// Syscalls from newlib
extern int __real__open_r(struct _reent *r, char *path, int flags, int mode);
extern int __real__close_r(struct _reent *r, int fd);
extern ssize_t __real__read_r(struct _reent *r, int fd, void *dst, size_t size);
extern ssize_t __real__write_r(struct _reent *r, int fd, void *buf, size_t size);
extern int __real__fstat_r(struct _reent *r, int fd, struct stat *st);
extern off_t __real__lseek_r(struct _reent *r, int fd, off_t size, int mode);
extern int __real__stat_r(struct _reent *r, char *path, struct stat *st);
extern int __real__link_r(struct _reent *r, char *n1, char *n2);
extern int __real__unlink_r(struct _reent *r, char *path);
extern int __real__rename_r(struct _reent *r, char *src, char *dst);

// Directory operations
extern DIR *__real_opendir(char *name);
extern struct dirent *__real_readdir(DIR *pdir);
extern int __real_closedir(DIR *pdir);
extern int __real_mkdir(char *path, mode_t mode);

int __wrap__open_r(struct _reent *r, char *path, int flags, int mode) {
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  bool prev_h = eos_proc_heap_tracking_disable();
  int fd = __real__open_r(r, _path, flags, mode);
  eos_proc_heap_tracking_restore(prev_h);

  if (fd >= 0) {
    eos_vfs_fd_register(fd, flags);
    eos_proc_t *proc = eos_proc_current();
    if (proc && fd < FD_SETSIZE) {
      eos_proc_fd_map_set(proc, fd, -1);
    }
    eos_proc_reg_fd(fd);
  }

  return fd;
}

int __wrap__close_r(struct _reent *r, int fd) {
  eos_proc_t *proc = eos_proc_current();

  // If closing a virtual redirected descriptor (e.g. dup2 target),
  // clear its mapping without closing the underlying target unless owned.
  if (proc && fd >= 0 && fd < FD_SETSIZE && eos_proc_fd_map_get(proc, fd) >= 0) {
    eos_proc_fd_map_set(proc, fd, -1);
    return 0;
  }

  if (fd < 0) {
    bool prev_h = eos_proc_heap_tracking_disable();
    int ret = __real__close_r(r, fd);
    eos_proc_heap_tracking_restore(prev_h);
    return ret;
  }

  eos_vfs_file_t *file = eos_vfs_file_close_start(fd);

  if (proc) {
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

  // Perform underlying physical close to trigger wakeups (e.g. pipefs r_cond/w_cond)
  // No central VFS lock is held here so blocked peers can wake and complete.
  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__close_r(r, fd);
  eos_proc_heap_tracking_restore(prev_h);

  if (file) {
    eos_vfs_file_release(file);
  }

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
  int real_fd = resolve_read_fd(fd);
  if (real_fd == STDIN_FILENO && !eos_console_is_connected()) {
    errno = EAGAIN;
    return -1;
  }

  if (real_fd >= 0) {
    eos_vfs_file_t *file = eos_vfs_file_acquire(real_fd);
    if (!file) {
      __errno_r(r) = EBADF;
      return -1;
    }
    ssize_t ret = __real__read_r(r, real_fd, dst, size);
    eos_vfs_file_release(file);
    return ret;
  }

  return __real__read_r(r, real_fd, dst, size);
}

ssize_t __wrap__write_r(struct _reent *r, int fd, void *buf, size_t size) {
  int real_fd = resolve_write_fd(fd);
  if (real_fd >= 0) {
    eos_vfs_file_t *file = eos_vfs_file_acquire(real_fd);
    if (!file) {
      __errno_r(r) = EBADF;
      return -1;
    }
    ssize_t ret = __real__write_r(r, real_fd, buf, size);
    eos_vfs_file_release(file);
    return ret;
  }

  return __real__write_r(r, real_fd, buf, size);
}

off_t __wrap__lseek_r(struct _reent *r, int fd, off_t size, int mode) {
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

  if (real_fd >= 0) {
    eos_vfs_file_t *file = eos_vfs_file_acquire(real_fd);
    if (!file) {
      __errno_r(r) = EBADF;
      return -1;
    }
    pthread_mutex_lock(&file->offset_lock);
    off_t ret = __real__lseek_r(r, real_fd, size, mode);
    pthread_mutex_unlock(&file->offset_lock);
    eos_vfs_file_release(file);
    return ret;
  }

  return __real__lseek_r(r, real_fd, size, mode);
}

int __wrap__fstat_r(struct _reent *r, int fd, struct stat *st) {
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

  if (real_fd >= 0) {
    eos_vfs_file_t *file = eos_vfs_file_acquire(real_fd);
    if (!file) {
      __errno_r(r) = EBADF;
      return -1;
    }
    int ret = __real__fstat_r(r, real_fd, st);
    eos_vfs_file_release(file);
    return ret;
  }

  return __real__fstat_r(r, real_fd, st);
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

  // Allocate a virtual descriptor slot from the top down to avoid
  // colliding with ESP-IDF VFS descriptor numbers.
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
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__stat_r(r, _path, st);
  eos_proc_heap_tracking_restore(prev_h);

  return ret;
}

int __wrap__link_r(struct _reent *r, char *n1, char *n2) {
  char b1[PATH_MAX], b2[PATH_MAX];
  char *_n1 = eos_fapi_path_resolve(n1, b1);
  char *_n2 = eos_fapi_path_resolve(n2, b2);

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__link_r(r, _n1, _n2);
  eos_proc_heap_tracking_restore(prev_h);

  return ret;
}

int __wrap__unlink_r(struct _reent *r, char *path) {
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__unlink_r(r, _path);
  eos_proc_heap_tracking_restore(prev_h);

  return ret;
}

int __wrap__rename_r(struct _reent *r, char *src, char *dst) {
  char b1[PATH_MAX], b2[PATH_MAX];
  char *_src = eos_fapi_path_resolve(src, b1);
  char *_dst = eos_fapi_path_resolve(dst, b2);

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__rename_r(r, _src, _dst);
  eos_proc_heap_tracking_restore(prev_h);

  return ret;
}

DIR *__wrap_opendir(char *name) {
  char buf[PATH_MAX];
  char *_name = eos_fapi_path_resolve(name, buf);

  bool prev_h = eos_proc_heap_tracking_disable();
  DIR *pdir = __real_opendir(_name);
  eos_proc_heap_tracking_restore(prev_h);

  if (pdir) {
    eos_vfs_dir_register(pdir);
    eos_proc_reg_dir(pdir);
  }

  return pdir;
}

struct dirent *__wrap_readdir(DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return NULL;
  }

  eos_vfs_dir_t *dir = eos_vfs_dir_acquire(pdir);
  if (!dir) {
    errno = EBADF;
    return NULL;
  }

  pthread_mutex_lock(&dir->lock);
  bool prev_h = eos_proc_heap_tracking_disable();
  struct dirent *ent = __real_readdir(pdir);
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&dir->lock);
  eos_vfs_dir_release(dir);

  return ent;
}

int __wrap_closedir(DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return -1;
  }

  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    eos_proc_unreg_dir(pdir);
  }

  eos_vfs_dir_t *dir = eos_vfs_dir_close_start(pdir);

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real_closedir(pdir);
  eos_proc_heap_tracking_restore(prev_h);

  if (dir) {
    eos_vfs_dir_release(dir);
  }

  return ret;
}

int __wrap_mkdir(char *path, mode_t mode) {
  char buf[PATH_MAX];
  char *_path = eos_fapi_path_resolve(path, buf);

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real_mkdir(_path, mode);
  eos_proc_heap_tracking_restore(prev_h);

  return ret;
}
