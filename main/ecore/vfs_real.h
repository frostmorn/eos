#pragma once

#include <dirent.h>
#include <reent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Declarations of underlying real libc/VFS syscalls wrapped by EOS
extern int __real__open_r(struct _reent *r, const char *path, int flags, int mode);
extern int __real__close_r(struct _reent *r, int fd);
extern ssize_t __real__read_r(struct _reent *r, int fd, void *dst, size_t size);
extern ssize_t __real__write_r(struct _reent *r, int fd, const void *buf, size_t size);
extern off_t __real__lseek_r(struct _reent *r, int fd, off_t size, int mode);
extern int __real__fstat_r(struct _reent *r, int fd, struct stat *st);
extern int __real__stat_r(struct _reent *r, const char *path, struct stat *st);
extern int __real__link_r(struct _reent *r, const char *n1, const char *n2);
extern int __real__unlink_r(struct _reent *r, const char *path);
extern int __real__rename_r(struct _reent *r, const char *src, const char *dst);
extern DIR *__real_opendir(const char *name);
extern struct dirent *__real_readdir(DIR *pdir);
extern int __real_closedir(DIR *pdir);
extern void __real_seekdir(DIR *pdir, long loc);
extern long __real_telldir(DIR *pdir);
extern int __real_mkdir(const char *path, mode_t mode);
extern int __real_rmdir(const char *path);
extern int __real_access(const char *path, int amode);
extern int __real_ioctl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif
