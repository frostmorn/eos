#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

// Global VFS Allocation Lock (recursive mutex for serializing open / opendir and descriptor table mutations)
void eos_vfs_alloc_lock(void);
void eos_vfs_alloc_unlock(void);

typedef struct {
  pthread_mutex_t lock;
  atomic_int ref_count;
  int flags;
  bool in_use;
} eos_vfs_fd_desc_t;

typedef struct {
  DIR *pdir;
  pthread_mutex_t lock;
  atomic_int ref_count;
  bool in_use;
} eos_vfs_dir_desc_t;

// Initialize VFS synchronization primitives and dynamic PSRAM descriptor tables upfront
void eos_vfs_init(void);

// File descriptor operations (backed by dynamic kvec in PSRAM)
void eos_vfs_fd_register(int fd, int flags);
int eos_vfs_fd_ref(int fd);
int eos_vfs_fd_unref(int fd);
void eos_vfs_fd_lock(int fd);
void eos_vfs_fd_unlock(int fd);
void eos_vfs_fd_unregister(int fd);
bool eos_vfs_fd_is_valid(int fd);

// Directory stream operations (backed by dynamic kvec in PSRAM)
void eos_vfs_dir_register(DIR *pdir);
void eos_vfs_dir_lock(DIR *pdir);
void eos_vfs_dir_unlock(DIR *pdir);
void eos_vfs_dir_close(DIR *pdir);
