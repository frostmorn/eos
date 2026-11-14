#pragma once

#include <dirent.h>
#include <esp_err.h>
#include <esp_vfs.h>
#include <fcntl.h>
#include <pthread.h>
#include <reent.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

struct eos_vfs_mount;

// Short table lock for atomic VFS table mutations (never held across I/O)
void eos_vfs_alloc_lock(void);
void eos_vfs_alloc_unlock(void);
void eos_vfs_table_lock(void);
void eos_vfs_table_unlock(void);

// Base open file description structure (PSRAM allocated, refcounted).
// Filesystems embed this as the first member of their file context:
// struct my_file { eos_vfs_file_t base; ... };
typedef struct eos_vfs_file {
  int fd;                                // EOS virtual file descriptor (>= 3, or 0..2 for stdio)
  const struct eos_vfs_mount *mount;     // Filesystem mount owning this file (NULL for native ESP-IDF)
  int flags;
  atomic_int ref_count;
  atomic_bool closing;
  pthread_mutex_t offset_lock;           // Serializes file offset on regular files only
  void *fs_data;                         // Private pointer or extra data
} eos_vfs_file_t;

typedef eos_vfs_file_t eos_vfs_fd_desc_t;

// Base open directory description structure (PSRAM allocated, refcounted).
// Filesystems embed this as the first member of their directory context:
// struct my_dir { eos_vfs_dir_t base; struct dirent entry; ... };
typedef struct eos_vfs_dir {
  DIR *pdir;                             // Exposed DIR stream pointer
  const struct eos_vfs_mount *mount;     // Filesystem mount owning this dir (NULL for native ESP-IDF)
  atomic_int ref_count;
  atomic_bool closing;
  pthread_mutex_t lock;                  // Serializes readdir/telldir on this DIR*
  void *fs_data;                         // Private pointer or extra data
} eos_vfs_dir_t;

typedef eos_vfs_dir_t eos_vfs_dir_desc_t;

// Unified VFS operations table.
// Callbacks receive the enlarged eos_vfs_file_t / eos_vfs_dir_t structures directly.
typedef struct eos_vfs_ops {
  int (*open)(void *ctx, const char *path, int flags, int mode, eos_vfs_file_t **out_file);
  int (*close)(void *ctx, eos_vfs_file_t *file);
  ssize_t (*read)(void *ctx, eos_vfs_file_t *file, void *dst, size_t size);
  ssize_t (*write)(void *ctx, eos_vfs_file_t *file, const void *src, size_t size);
  off_t (*lseek)(void *ctx, eos_vfs_file_t *file, off_t offset, int whence);
  int (*fstat)(void *ctx, eos_vfs_file_t *file, struct stat *st);
  int (*stat)(void *ctx, const char *path, struct stat *st);
  int (*link)(void *ctx, const char *oldpath, const char *newpath);
  int (*unlink)(void *ctx, const char *path);
  int (*rename)(void *ctx, const char *src, const char *dst);
  int (*mkdir)(void *ctx, const char *path, mode_t mode);
  int (*rmdir)(void *ctx, const char *path);
  int (*access)(void *ctx, const char *path, int amode);
  int (*ioctl)(void *ctx, eos_vfs_file_t *file, int cmd, va_list args);
  int (*opendir)(void *ctx, const char *name, eos_vfs_dir_t **out_dir);
  struct dirent *(*readdir)(void *ctx, eos_vfs_dir_t *dir);
  int (*closedir)(void *ctx, eos_vfs_dir_t *dir);
  void (*seekdir)(void *ctx, eos_vfs_dir_t *dir, long loc);
  long (*telldir)(void *ctx, eos_vfs_dir_t *dir);
  int (*symlink)(void *ctx, const char *target, const char *linkpath);
  ssize_t (*readlink)(void *ctx, const char *path, char *buf, size_t bufsiz);
  int (*lstat)(void *ctx, const char *path, struct stat *st);
} eos_vfs_ops_t;

// Central VFS mount structure
typedef struct eos_vfs_mount {
  char prefix[64];
  size_t prefix_len;
  eos_vfs_ops_t ops;
  void *ctx;
  bool in_use;
} eos_vfs_mount_t;

// Initialize VFS synchronization primitives and dynamic PSRAM descriptor tables upfront
void eos_vfs_init(void);

// Allocation of enlarged file and directory structures in PSRAM
void *eos_vfs_file_alloc(const eos_vfs_mount_t *mount, size_t total_size, int flags);
void *eos_vfs_dir_alloc(const eos_vfs_mount_t *mount, size_t total_size);

// Mount management
esp_err_t eos_vfs_register_ops(const char *base_path, const eos_vfs_ops_t *ops, void *ctx);
esp_err_t eos_vfs_register(const char *base_path, const esp_vfs_t *vfs, void *ctx);
esp_err_t eos_vfs_unregister(const char *base_path);
const eos_vfs_mount_t *eos_vfs_match_mount(const char *path, const char **out_rel_path);

// Centralized VFS dispatch operations
int eos_vfs_open_r(struct _reent *r, const char *path, int flags, int mode);
int eos_vfs_close_r(struct _reent *r, int fd);
ssize_t eos_vfs_read_r(struct _reent *r, int fd, void *dst, size_t size);
ssize_t eos_vfs_write_r(struct _reent *r, int fd, const void *src, size_t size);
off_t eos_vfs_lseek_r(struct _reent *r, int fd, off_t offset, int whence);
int eos_vfs_fstat_r(struct _reent *r, int fd, struct stat *st);
int eos_vfs_stat_r(struct _reent *r, const char *path, struct stat *st);
int eos_vfs_link_r(struct _reent *r, const char *oldpath, const char *newpath);
int eos_vfs_unlink_r(struct _reent *r, const char *path);
int eos_vfs_rename_r(struct _reent *r, const char *src, const char *dst);
DIR *eos_vfs_opendir(const char *name);
struct dirent *eos_vfs_readdir(DIR *pdir);
int eos_vfs_closedir(DIR *pdir);
void eos_vfs_seekdir(DIR *pdir, long loc);
long eos_vfs_telldir(DIR *pdir);
void eos_vfs_rewinddir(DIR *pdir);
int eos_vfs_mkdir(const char *path, mode_t mode);
int eos_vfs_rmdir(const char *path);
int eos_vfs_access(const char *path, int amode);
int eos_vfs_ioctl(int fd, int cmd, va_list args);

// Symlinks and link traversal
int eos_vfs_symlink(const char *target, const char *linkpath);
ssize_t eos_vfs_readlink(const char *path, char *buf, size_t bufsiz);
int eos_vfs_lstat(const char *path, struct stat *st);
int eos_vfs_resolve_symlinks(const char *path, char *out_buf, size_t out_size);

// Diagnostic dump
void eos_vfs_dump_fds(FILE *f);

// File descriptor operations (backed by dynamic kvec in PSRAM)
void eos_vfs_fd_register(int fd, int flags);
eos_vfs_file_t *eos_vfs_file_acquire(int fd);
void eos_vfs_file_release(eos_vfs_file_t *file);
eos_vfs_file_t *eos_vfs_file_close_start(int fd);
bool eos_vfs_fd_is_valid(int fd);

// Stubs for backward compatibility
int eos_vfs_fd_ref(int fd);
int eos_vfs_fd_unref(int fd);
void eos_vfs_fd_lock(int fd);
void eos_vfs_fd_unlock(int fd);
void eos_vfs_fd_unregister(int fd);

// Directory stream operations (backed by dynamic kvec in PSRAM)
void eos_vfs_dir_register(DIR *pdir);
eos_vfs_dir_t *eos_vfs_dir_acquire(DIR *pdir);
void eos_vfs_dir_release(eos_vfs_dir_t *dir);
eos_vfs_dir_t *eos_vfs_dir_close_start(DIR *pdir);
void eos_vfs_dir_lock(DIR *pdir);
void eos_vfs_dir_unlock(DIR *pdir);
void eos_vfs_dir_close(DIR *pdir);
