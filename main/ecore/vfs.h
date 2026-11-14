#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

// Short table lock for atomic VFS table mutations (never held across I/O)
void eos_vfs_alloc_lock(void);
void eos_vfs_alloc_unlock(void);
void eos_vfs_table_lock(void);
void eos_vfs_table_unlock(void);

// Open file description structure (PSRAM allocated, refcounted)
typedef struct eos_vfs_file {
  int fd;
  int flags;
  atomic_int ref_count;
  atomic_bool closing;
  pthread_mutex_t offset_lock; // Serializes file offset on regular files only
} eos_vfs_file_t;

typedef eos_vfs_file_t eos_vfs_fd_desc_t;

// Open directory description structure (PSRAM allocated, refcounted)
typedef struct eos_vfs_dir {
  DIR *pdir;
  atomic_int ref_count;
  atomic_bool closing;
  pthread_mutex_t lock; // Serializes readdir/telldir on the same DIR*
} eos_vfs_dir_t;

typedef eos_vfs_dir_t eos_vfs_dir_desc_t;

// Initialize VFS synchronization primitives and dynamic PSRAM descriptor tables upfront
void eos_vfs_init(void);

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
