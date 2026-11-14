#include "pipefs.h"
#include "ecore/proc.h"
#include "ecore/vfs.h"
#include "emisc/fancymacro.h"
#include "emisc/kvec.h"
#include "emisc/psram_alloc.h"

#include <dirent.h>
#include <errno.h>
#include <esp_vfs.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////
// Pipe Node & FD structures
///////////////////////////////////////////////////////////////////

typedef struct {
  bool in_use;
  bool unlinked;
  char name[EOS_XSMALL_STR_LEN];
  uint8_t buf[EOS_PIPE_BUF_SIZE];
  size_t head;
  size_t tail;
  size_t count;
  int readers;
  int writers;
  bool ever_written;
  pthread_mutex_t lock;
  pthread_cond_t r_cond;
  pthread_cond_t w_cond;
} pipe_node_t;

typedef struct {
  eos_vfs_file_t base;
  int pipe_idx;
} pipe_file_t;

typedef struct {
  eos_vfs_dir_t base;
  uint32_t idx;
  struct dirent entry;
} pipe_dir_t;

typedef struct {
  pthread_mutex_t lock;
  kvec_psram_t(pipe_node_t *) nodes;
  uint32_t auto_seq;
  bool initialized;
} eos_pipe_table_t;

static eos_pipe_table_t *s_pipe_table = NULL;

///////////////////////////////////////////////////////////////////
// Internal helpers
///////////////////////////////////////////////////////////////////

static pipe_node_t *pipe_find_by_name_locked(const char *name, int *out_idx) {
  if (!s_pipe_table || !name || !name[0])
    return NULL;

  for (size_t i = 0; i < kv_size(s_pipe_table->nodes); i++) {
    pipe_node_t *node = kv_A(s_pipe_table->nodes, i);
    if (node && node->in_use && !node->unlinked &&
        strcmp(node->name, name) == 0) {
      if (out_idx)
        *out_idx = (int)i;
      return node;
    }
  }
  return NULL;
}

static int pipe_alloc_node_locked(const char *name) {
  if (!s_pipe_table)
    return -1;

  if (name && name[0]) {
    if (strlen(name) >= EOS_XSMALL_STR_LEN) {
      errno = ENAMETOOLONG;
      return -1;
    }
    if (pipe_find_by_name_locked(name, NULL)) {
      errno = EEXIST;
      return -1;
    }
  }

  // Reuse an existing inactive node slot if available
  for (size_t i = 0; i < kv_size(s_pipe_table->nodes); i++) {
    pipe_node_t *node = kv_A(s_pipe_table->nodes, i);
    if (node && !node->in_use) {
      memset(node->buf, 0, sizeof(node->buf));
      node->head = 0;
      node->tail = 0;
      node->count = 0;
      node->readers = 0;
      node->writers = 0;
      node->ever_written = false;
      node->unlinked = false;
      node->in_use = true;
      bool prev_h = eos_proc_heap_tracking_disable();
      pthread_cond_destroy(&node->r_cond);
      pthread_cond_destroy(&node->w_cond);
      pthread_cond_init(&node->r_cond, NULL);
      pthread_cond_init(&node->w_cond, NULL);
      eos_proc_heap_tracking_restore(prev_h);

      if (name && name[0]) {
        strlcpy(node->name, name, sizeof(node->name));
      } else {
        snprintf(node->name, sizeof(node->name), "p%" PRIu32, s_pipe_table->auto_seq++);
      }
      return (int)i;
    }
  }

  // Allocate new node dynamically in PSRAM
  bool prev_h = eos_proc_heap_tracking_disable();
  pipe_node_t *new_node = eos_psram_calloc_prefer(1, sizeof(pipe_node_t));
  if (!new_node) {
    eos_proc_heap_tracking_restore(prev_h);
    errno = ENOMEM;
    return -1;
  }
  pthread_mutex_init(&new_node->lock, NULL);
  pthread_cond_init(&new_node->r_cond, NULL);
  pthread_cond_init(&new_node->w_cond, NULL);
  new_node->in_use = true;
  if (name && name[0]) {
    strlcpy(new_node->name, name, sizeof(new_node->name));
  } else {
    snprintf(new_node->name, sizeof(new_node->name), "p%" PRIu32, s_pipe_table->auto_seq++);
  }
  int idx = (int)kv_size(s_pipe_table->nodes);
  kv_push(pipe_node_t *, s_pipe_table->nodes, new_node);
  eos_proc_heap_tracking_restore(prev_h);
  return idx;
}

static void pipe_check_reclaim_locked(int idx) {
  if (!s_pipe_table || idx < 0 || (size_t)idx >= kv_size(s_pipe_table->nodes))
    return;

  pipe_node_t *node = kv_A(s_pipe_table->nodes, idx);
  if (!node || !node->in_use)
    return;

  // Reclaim anonymous/unlinked pipes when all ends are closed, or
  // auto-reclaim any pipe once a writer has opened and both readers and
  // writers drop to 0 with no remaining unread bytes.
  if (node->readers <= 0 && node->writers <= 0) {
    if (node->unlinked || (node->ever_written && node->count == 0)) {
      node->in_use = false;
      node->unlinked = false;
      node->name[0] = '\0';
      node->head = 0;
      node->tail = 0;
      node->count = 0;
      bool prev_h = eos_proc_heap_tracking_disable();
      pthread_cond_destroy(&node->r_cond);
      pthread_cond_destroy(&node->w_cond);
      pthread_cond_init(&node->r_cond, NULL);
      pthread_cond_init(&node->w_cond, NULL);
      eos_proc_heap_tracking_restore(prev_h);
    }
  }
}

///////////////////////////////////////////////////////////////////
// VFS Callbacks
///////////////////////////////////////////////////////////////////

static int pipefs_open(void *ctx, const char *path, int flags, int mode, eos_vfs_file_t **out_file) {
  (void)ctx;
  (void)mode;

  if (!path) {
    errno = EINVAL;
    return -1;
  }

  while (*path == '/')
    path++;

  if (*path == '\0') {
    errno = EISDIR;
    return -1;
  }

  if (!s_pipe_table) {
    errno = ENODEV;
    return -1;
  }

  pthread_mutex_lock(&s_pipe_table->lock);

  int pipe_idx = -1;
  pipe_node_t *node = pipe_find_by_name_locked(path, &pipe_idx);

  if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
    pthread_mutex_unlock(&s_pipe_table->lock);
    errno = EEXIST;
    return -1;
  }

  if (!node) {
    // Allow creating on O_CREAT or when opened for writing
    int acc = flags & O_ACCMODE;
    if ((flags & O_CREAT) || acc == O_WRONLY || acc == O_RDWR) {
      pipe_idx = pipe_alloc_node_locked(path);
      if (pipe_idx < 0) {
        pthread_mutex_unlock(&s_pipe_table->lock);
        return -1;
      }
      node = kv_A(s_pipe_table->nodes, pipe_idx);
    } else {
      pthread_mutex_unlock(&s_pipe_table->lock);
      errno = ENOENT;
      return -1;
    }
  }

  pipe_file_t *file = eos_vfs_file_alloc(NULL, sizeof(pipe_file_t), flags);
  if (!file) {
    pipe_check_reclaim_locked(pipe_idx);
    pthread_mutex_unlock(&s_pipe_table->lock);
    errno = ENFILE;
    return -1;
  }
  file->pipe_idx = pipe_idx;

  pthread_mutex_lock(&node->lock);
  int acc = flags & O_ACCMODE;
  if (acc == O_RDONLY) {
    node->readers++;
  } else if (acc == O_WRONLY) {
    node->writers++;
    node->ever_written = true;
  } else if (acc == O_RDWR) {
    node->readers++;
    node->writers++;
    node->ever_written = true;
  }

  if (flags & O_TRUNC) {
    node->head = 0;
    node->tail = 0;
    node->count = 0;
  }

  pthread_cond_broadcast(&node->r_cond);
  pthread_cond_broadcast(&node->w_cond);
  pthread_mutex_unlock(&node->lock);

  pthread_mutex_unlock(&s_pipe_table->lock);
  *out_file = &file->base;
  return file->base.fd;
}

static int pipefs_close(void *ctx, eos_vfs_file_t *file) {
  (void)ctx;
  if (!s_pipe_table || !file) {
    return 0;
  }

  pipe_file_t *pf = (pipe_file_t *)file;
  int pipe_idx = pf->pipe_idx;
  int flags = file->flags;

  pthread_mutex_lock(&s_pipe_table->lock);
  pipe_node_t *node = (pipe_idx >= 0 && (size_t)pipe_idx < kv_size(s_pipe_table->nodes))
                          ? kv_A(s_pipe_table->nodes, pipe_idx)
                          : NULL;
  if (node) {
    pthread_mutex_lock(&node->lock);

    int acc = flags & O_ACCMODE;
    if (acc == O_RDONLY) {
      if (node->readers > 0)
        node->readers--;
    } else if (acc == O_WRONLY) {
      if (node->writers > 0)
        node->writers--;
    } else if (acc == O_RDWR) {
      if (node->readers > 0)
        node->readers--;
      if (node->writers > 0)
        node->writers--;
    }

    pthread_cond_broadcast(&node->r_cond);
    pthread_cond_broadcast(&node->w_cond);
    pthread_mutex_unlock(&node->lock);
  }

  pipe_check_reclaim_locked(pipe_idx);
  pthread_mutex_unlock(&s_pipe_table->lock);
  return 0;
}

static ssize_t pipefs_read(void *ctx, eos_vfs_file_t *file, void *dst, size_t size) {
  (void)ctx;
  if (!dst && size > 0) {
    errno = EINVAL;
    return -1;
  }
  if (size == 0)
    return 0;

  if (!s_pipe_table || !file) {
    errno = ENODEV;
    return -1;
  }

  pipe_file_t *pf = (pipe_file_t *)file;
  int pipe_idx = pf->pipe_idx;
  int flags = file->flags;
  if ((flags & O_ACCMODE) == O_WRONLY) {
    errno = EBADF;
    return -1;
  }

  pthread_mutex_lock(&s_pipe_table->lock);
  pipe_node_t *node = (pipe_idx >= 0 && (size_t)pipe_idx < kv_size(s_pipe_table->nodes))
                          ? kv_A(s_pipe_table->nodes, pipe_idx)
                          : NULL;
  if (!node) {
    pthread_mutex_unlock(&s_pipe_table->lock);
    errno = EBADF;
    return -1;
  }
  pthread_mutex_lock(&node->lock);
  pthread_mutex_unlock(&s_pipe_table->lock);

  uint8_t *out = (uint8_t *)dst;
  size_t total_read = 0;

  while (total_read == 0) {
    while (node->count > 0 && total_read < size) {
      out[total_read++] = node->buf[node->tail];
      node->tail = (node->tail + 1) % EOS_PIPE_BUF_SIZE;
      node->count--;
    }

    if (total_read > 0) {
      pthread_cond_broadcast(&node->w_cond);
      break;
    }

    // Buffer is empty: if no active writers or readers remain, return EOF (0)
    if (node->writers == 0 || node->readers == 0) {
      break;
    }

    if (flags & O_NONBLOCK) {
      pthread_mutex_unlock(&node->lock);
      errno = EAGAIN;
      return -1;
    }

    bool prev_w = eos_proc_enter_wait();
    pthread_cond_wait(&node->r_cond, &node->lock);
    eos_proc_leave_wait(prev_w);
  }

  pthread_mutex_unlock(&node->lock);

  if (total_read == 0) {
    pthread_mutex_lock(&s_pipe_table->lock);
    pipe_check_reclaim_locked(pipe_idx);
    pthread_mutex_unlock(&s_pipe_table->lock);
  }

  return (ssize_t)total_read;
}

static ssize_t pipefs_write(void *ctx, eos_vfs_file_t *file, const void *data, size_t size) {
  (void)ctx;
  if (!data && size > 0) {
    errno = EINVAL;
    return -1;
  }
  if (size == 0)
    return 0;

  if (!s_pipe_table || !file) {
    errno = ENODEV;
    return -1;
  }

  pipe_file_t *pf = (pipe_file_t *)file;
  int pipe_idx = pf->pipe_idx;
  int flags = file->flags;
  if ((flags & O_ACCMODE) == O_RDONLY) {
    errno = EBADF;
    return -1;
  }

  pthread_mutex_lock(&s_pipe_table->lock);
  pipe_node_t *node = (pipe_idx >= 0 && (size_t)pipe_idx < kv_size(s_pipe_table->nodes))
                          ? kv_A(s_pipe_table->nodes, pipe_idx)
                          : NULL;
  if (!node) {
    pthread_mutex_unlock(&s_pipe_table->lock);
    errno = EBADF;
    return -1;
  }
  pthread_mutex_lock(&node->lock);
  pthread_mutex_unlock(&s_pipe_table->lock);

  const uint8_t *src = (const uint8_t *)data;
  size_t total_written = 0;

  while (total_written < size) {
    // If pipe was unlinked/anonymous and all readers closed, report EPIPE
    if (node->unlinked && node->readers == 0) {
      pthread_mutex_unlock(&node->lock);
      errno = EPIPE;
      return total_written > 0 ? (ssize_t)total_written : -1;
    }

    while (node->count < EOS_PIPE_BUF_SIZE && total_written < size) {
      node->buf[node->head] = src[total_written++];
      node->head = (node->head + 1) % EOS_PIPE_BUF_SIZE;
      node->count++;
    }

    if (total_written > 0) {
      pthread_cond_broadcast(&node->r_cond);
    }

    if (total_written == size)
      break;

    // Buffer is full; if no readers are open and pipe is not anonymous,
    // wait for a reader unless O_NONBLOCK is set.
    if (flags & O_NONBLOCK) {
      pthread_mutex_unlock(&node->lock);
      if (total_written > 0)
        return (ssize_t)total_written;
      errno = EAGAIN;
      return -1;
    }

    bool prev_w = eos_proc_enter_wait();
    pthread_cond_wait(&node->w_cond, &node->lock);
    eos_proc_leave_wait(prev_w);
  }

  pthread_mutex_unlock(&node->lock);
  return (ssize_t)total_written;
}

static int pipefs_fstat(void *ctx, eos_vfs_file_t *file, struct stat *st) {
  (void)ctx;
  if (!st || !file || !s_pipe_table) {
    errno = EINVAL;
    return -1;
  }

  pipe_file_t *pf = (pipe_file_t *)file;
  int pipe_idx = pf->pipe_idx;

  pthread_mutex_lock(&s_pipe_table->lock);
  pipe_node_t *node = (pipe_idx >= 0 && (size_t)pipe_idx < kv_size(s_pipe_table->nodes))
                          ? kv_A(s_pipe_table->nodes, pipe_idx)
                          : NULL;
  if (!node) {
    pthread_mutex_unlock(&s_pipe_table->lock);
    errno = EBADF;
    return -1;
  }
  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFIFO | 0666;
  st->st_size = (off_t)node->count;
  pthread_mutex_unlock(&s_pipe_table->lock);
  return 0;
}

static int pipefs_stat(void *ctx, const char *path, struct stat *st) {
  (void)ctx;
  if (!path || !st) {
    errno = EINVAL;
    return -1;
  }

  while (*path == '/')
    path++;

  if (*path == '\0') {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0755;
    return 0;
  }

  if (!s_pipe_table) {
    errno = ENODEV;
    return -1;
  }

  pthread_mutex_lock(&s_pipe_table->lock);
  pipe_node_t *node = pipe_find_by_name_locked(path, NULL);
  if (!node) {
    pthread_mutex_unlock(&s_pipe_table->lock);
    errno = ENOENT;
    return -1;
  }
  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFIFO | 0666;
  st->st_size = (off_t)node->count;
  pthread_mutex_unlock(&s_pipe_table->lock);
  return 0;
}

static int pipefs_unlink(void *ctx, const char *path) {
  (void)ctx;
  if (!path) {
    errno = EINVAL;
    return -1;
  }

  while (*path == '/')
    path++;

  if (*path == '\0') {
    errno = EBUSY;
    return -1;
  }

  if (!s_pipe_table) {
    errno = ENODEV;
    return -1;
  }

  pthread_mutex_lock(&s_pipe_table->lock);
  int idx = -1;
  pipe_node_t *node = pipe_find_by_name_locked(path, &idx);
  if (!node) {
    pthread_mutex_unlock(&s_pipe_table->lock);
    errno = ENOENT;
    return -1;
  }

  node->unlinked = true;
  pipe_check_reclaim_locked(idx);
  pthread_mutex_unlock(&s_pipe_table->lock);
  return 0;
}

static int pipefs_opendir(void *ctx, const char *path, eos_vfs_dir_t **out_dir) {
  (void)ctx;
  if (!path || (strcmp(path, "/") != 0 && strcmp(path, "") != 0)) {
    errno = ENOTDIR;
    return -1;
  }

  pipe_dir_t *dir = eos_vfs_dir_alloc(NULL, sizeof(pipe_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return -1;
  }
  dir->idx = 0;
  *out_dir = &dir->base;
  return 0;
}

static struct dirent *pipefs_readdir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  pipe_dir_t *dir = (pipe_dir_t *)pdir;
  if (!dir || !s_pipe_table) {
    errno = EBADF;
    return NULL;
  }

  pthread_mutex_lock(&s_pipe_table->lock);
  while (dir->idx < kv_size(s_pipe_table->nodes)) {
    uint32_t i = dir->idx++;
    pipe_node_t *node = kv_A(s_pipe_table->nodes, i);
    if (node && node->in_use && !node->unlinked) {
      memset(&dir->entry, 0, sizeof(dir->entry));
      dir->entry.d_ino = i;
      dir->entry.d_type = DT_FIFO;
      strlcpy(dir->entry.d_name, node->name, sizeof(dir->entry.d_name));
      pthread_mutex_unlock(&s_pipe_table->lock);
      return &dir->entry;
    }
  }
  pthread_mutex_unlock(&s_pipe_table->lock);
  return NULL;
}

static int pipefs_closedir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  (void)pdir;
  return 0;
}

static void pipefs_seekdir(void *ctx, eos_vfs_dir_t *pdir, long loc) {
  (void)ctx;
  pipe_dir_t *dir = (pipe_dir_t *)pdir;
  if (!dir) return;
  dir->idx = (uint32_t)loc;
}

static long pipefs_telldir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  pipe_dir_t *dir = (pipe_dir_t *)pdir;
  if (!dir) return -1;
  return (long)dir->idx;
}

///////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////

int eos_pipefs_create(char *path_out, size_t len) {
  if (!s_pipe_table)
    return -1;

  pthread_mutex_lock(&s_pipe_table->lock);
  int idx = pipe_alloc_node_locked(NULL);
  if (idx < 0) {
    pthread_mutex_unlock(&s_pipe_table->lock);
    return -1;
  }
  if (path_out && len > 0) {
    pipe_node_t *node = kv_A(s_pipe_table->nodes, idx);
    snprintf(path_out, len, EOS_PIPEFS_ROOT "/%s", node ? node->name : "");
  }
  pthread_mutex_unlock(&s_pipe_table->lock);
  return idx;
}

int pipe(int pipefd[2]) {
  if (!pipefd || !s_pipe_table) {
    errno = EINVAL;
    return -1;
  }

  char path[EOS_SMALL_STR_LEN];
  int idx = eos_pipefs_create(path, sizeof(path));
  if (idx < 0)
    return -1;

  int rfd = open(path, O_RDONLY);
  if (rfd < 0) {
    unlink(path);
    return -1;
  }

  int wfd = open(path, O_WRONLY);
  if (wfd < 0) {
    close(rfd);
    unlink(path);
    return -1;
  }

  // Mark anonymous pipe unlinked so it is automatically reclaimed
  // as soon as both descriptors are closed.
  pthread_mutex_lock(&s_pipe_table->lock);
  if (idx >= 0 && (size_t)idx < kv_size(s_pipe_table->nodes)) {
    pipe_node_t *node = kv_A(s_pipe_table->nodes, idx);
    if (node)
      node->unlinked = true;
  }
  pthread_mutex_unlock(&s_pipe_table->lock);

  pipefd[0] = rfd;
  pipefd[1] = wfd;
  return 0;
}

void eos_pipefs_init(void) {
  if (s_pipe_table && s_pipe_table->initialized)
    return;

  bool prev_h = eos_proc_heap_tracking_disable();
  s_pipe_table = eos_psram_calloc_prefer(1, sizeof(eos_pipe_table_t));
  if (!s_pipe_table) {
    eos_proc_heap_tracking_restore(prev_h);
    EOS_LOGE("Failed to allocate pipe table in PSRAM\n");
    return;
  }

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&s_pipe_table->lock, &attr);
  pthread_mutexattr_destroy(&attr);

  kv_init(s_pipe_table->nodes);
  s_pipe_table->auto_seq = 0;
  s_pipe_table->initialized = true;
  eos_proc_heap_tracking_restore(prev_h);

  static const eos_vfs_ops_t ops = {
      .open = pipefs_open,
      .close = pipefs_close,
      .read = pipefs_read,
      .write = pipefs_write,
      .stat = pipefs_stat,
      .fstat = pipefs_fstat,
      .unlink = pipefs_unlink,
      .opendir = pipefs_opendir,
      .readdir = pipefs_readdir,
      .closedir = pipefs_closedir,
      .seekdir = pipefs_seekdir,
      .telldir = pipefs_telldir,
  };

  mkdir(EOS_PIPEFS_ROOT, 0755);
  eos_vfs_register_ops(EOS_PIPEFS_ROOT, &ops, NULL);
  EOS_LOGI("pipefs mounted at %s\n", EOS_PIPEFS_ROOT);
}
