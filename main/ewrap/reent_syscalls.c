#include "ecore/fapi.h"
#include "ecore/log.h"
#include "ecore/proc.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

// Non syscalls
extern DIR *__real_opendir(char *name);
extern int __real_closedir(DIR *pdir);
extern int __real_mkdir(char *path, mode_t mode);

int __wrap__open_r(struct _reent *r, char *path, int flags, int mode) {
  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);
  int fd = __real__open_r(r, _path, flags, mode);

  if (fd >= 0) {
    eos_proc_t *proc = eos_proc_current();
    if (proc && fd < FD_SETSIZE) {
      proc->fd_map[fd] = -1;
    }
    eos_proc_reg_fd(fd);
  }

  return fd;
}

int __wrap__close_r(struct _reent *r, int fd) {
  eos_proc_t *proc = eos_proc_current();

  // If closing a virtual redirected descriptor (e.g. dup2 target),
  // clear its mapping without closing the underlying target unless owned.
  if (proc && fd >= 0 && fd < FD_SETSIZE && proc->fd_map[fd] >= 0) {
    proc->fd_map[fd] = -1;
    return 0;
  }

  int ret = __real__close_r(r, fd);

  if (ret == 0 && proc) {
    for (int i = 0; i < FD_SETSIZE; i++) {
      if (proc->fd_map[i] == fd) {
        proc->fd_map[i] = -1;
      }
    }
    eos_proc_unreg_fd(fd);
  }

  return ret;
}

static int resolve_read_fd(int fd) {
  eos_proc_t *proc = eos_proc_current();
  if (!proc) return fd;
  if (fd == STDIN_FILENO || (stdin && fd == fileno(stdin))) {
    return (proc->fd_map[STDIN_FILENO] >= 0) ? proc->fd_map[STDIN_FILENO] : fd;
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
    if (proc->fd_map[STDOUT_FILENO] >= 0) {
      return proc->fd_map[STDOUT_FILENO];
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

  return __real__read_r(r, real_fd, dst, size);
}

ssize_t __wrap__write_r(struct _reent *r, int fd, void *buf, size_t size) {
  return __real__write_r(r, resolve_write_fd(fd), buf, size);
}

off_t __wrap__lseek_r(struct _reent *r, int fd, off_t size, int mode) {
  int real_fd = fd;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    if (fd == STDIN_FILENO || (stdin && fd == fileno(stdin))) {
      real_fd = (proc->fd_map[STDIN_FILENO] >= 0) ? proc->fd_map[STDIN_FILENO] : fd;
    } else if (fd == STDOUT_FILENO || (stdout && fd == fileno(stdout))) {
      real_fd = resolve_write_fd(fd);
    } else {
      real_fd = eos_proc_resolve_fd(fd);
    }
  }
  return __real__lseek_r(r, real_fd, size, mode);
}

int __wrap__fstat_r(struct _reent *r, int fd, struct stat *st) {
  int real_fd = fd;
  eos_proc_t *proc = eos_proc_current();
  if (proc) {
    if (fd == STDIN_FILENO || (stdin && fd == fileno(stdin))) {
      real_fd = (proc->fd_map[STDIN_FILENO] >= 0) ? proc->fd_map[STDIN_FILENO] : fd;
    } else if (fd == STDOUT_FILENO || (stdout && fd == fileno(stdout))) {
      real_fd = resolve_write_fd(fd);
    } else {
      real_fd = eos_proc_resolve_fd(fd);
    }
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
    proc->fd_map[newfd] = -1;
  } else {
    proc->fd_map[newfd] = real_old;
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
  for (int vfd = FD_SETSIZE - 1; vfd >= 3; vfd--) {
    if (proc->fd_map[vfd] >= 0)
      continue;

    bool in_use = false;
    for (size_t i = 0; i < kv_size(proc->fds); i++) {
      if (kv_A(proc->fds, i) == vfd) {
        in_use = true;
        break;
      }
    }
    if (!in_use) {
      proc->fd_map[vfd] = real_old;
      return vfd;
    }
  }

  errno = EMFILE;
  return -1;
}

int __wrap__stat_r(struct _reent *r, char *path, struct stat *st) {
  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__stat_r(r, _path, st);
}

int __wrap__link_r(struct _reent *r, char *n1, char *n2) {
  char *_n1 = eos_fapi_get_buffer(0);
  _n1 = eos_fapi_path_resolve(n1, _n1);

  char *_n2 = eos_fapi_get_buffer(1);
  _n2 = eos_fapi_path_resolve(n2, _n2);

  return __real__link_r(r, _n1, _n2);
}

int __wrap__unlink_r(struct _reent *r, char *path) {
  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real__unlink_r(r, _path);
}

int __wrap__rename_r(struct _reent *r, char *src, char *dst) {
  char *_src = eos_fapi_get_buffer(0);
  _src = eos_fapi_path_resolve(src, _src);

  char *_dst = eos_fapi_get_buffer(1);
  _dst = eos_fapi_path_resolve(dst, _dst);

  return __real__rename_r(r, _src, _dst);
}

DIR *__wrap_opendir(char *name) {
  char *_name = eos_fapi_get_buffer(0);
  _name = eos_fapi_path_resolve(name, _name);
  
  DIR *pdir = __real_opendir(_name);
  
  if (pdir)
    eos_proc_reg_dir(pdir);

  return pdir;
}

int __wrap_closedir(DIR *pdir){
  int ret = __real_closedir(pdir);

  if (ret == 0)
    eos_proc_unreg_dir(pdir);

  return ret;
}

int __wrap_mkdir(char *path, mode_t mode) {
  char *_path = eos_fapi_get_buffer(0);
  _path = eos_fapi_path_resolve(path, _path);

  return __real_mkdir(_path, mode);
}
