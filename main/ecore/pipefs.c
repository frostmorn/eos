#include "pipefs.h"
#include "emisc/fancymacro.h"

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
  bool in_use;
  int pipe_idx;
  int flags;
} pipe_fd_t;

typedef struct {
  uint32_t esp_idf_fs_index;
  uint32_t idx;
  struct dirent entry;
} pipe_dir_t;

static pipe_node_t pipe_nodes[EOS_PIPEFS_MAX_PIPES];
static pipe_fd_t pipe_fds[EOS_PIPEFS_MAX_FDS];
static pthread_mutex_t pipe_table_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t pipe_auto_seq = 0;
static bool pipefs_initialized = false;

///////////////////////////////////////////////////////////////////
// Internal helpers
///////////////////////////////////////////////////////////////////

static pipe_node_t *pipe_find_by_name_locked(const char *name, int *out_idx) {
  if (!name || !name[0])
    return NULL;

  for (int i = 0; i < EOS_PIPEFS_MAX_PIPES; i++) {
    if (pipe_nodes[i].in_use && !pipe_nodes[i].unlinked &&
        strcmp(pipe_nodes[i].name, name) == 0) {
      if (out_idx)
        *out_idx = i;
      return &pipe_nodes[i];
    }
  }
  return NULL;
}

static int pipe_alloc_node_locked(const char *name) {
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

  for (int i = 0; i < EOS_PIPEFS_MAX_PIPES; i++) {
    if (!pipe_nodes[i].in_use) {
      pipe_node_t *node = &pipe_nodes[i];
      memset(node->buf, 0, sizeof(node->buf));
      node->head = 0;
      node->tail = 0;
      node->count = 0;
      node->readers = 0;
      node->writers = 0;
      node->ever_written = false;
      node->unlinked = false;
      node->in_use = true;

      if (name && name[0]) {
        strlcpy(node->name, name, sizeof(node->name));
      } else {
        snprintf(node->name, sizeof(node->name), "p%" PRIu32, pipe_auto_seq++);
      }
      return i;
    }
  }

  errno = ENFILE;
  return -1;
}

static void pipe_check_reclaim_locked(int idx) {
  if (idx < 0 || idx >= EOS_PIPEFS_MAX_PIPES)
    return;

  pipe_node_t *node = &pipe_nodes[idx];
  if (!node->in_use)
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
    }
  }
}

static int pipe_alloc_fd_locked(int pipe_idx, int flags) {
  for (int i = 0; i < EOS_PIPEFS_MAX_FDS; i++) {
    if (!pipe_fds[i].in_use) {
      pipe_fds[i].in_use = true;
      pipe_fds[i].pipe_idx = pipe_idx;
      pipe_fds[i].flags = flags;
      return i;
    }
  }
  errno = ENFILE;
  return -1;
}

///////////////////////////////////////////////////////////////////
// ESP-IDF VFS Callbacks
///////////////////////////////////////////////////////////////////

static int pipefs_open(void *ctx, const char *path, int flags, int mode) {
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

  pthread_mutex_lock(&pipe_table_lock);

  int pipe_idx = -1;
  pipe_node_t *node = pipe_find_by_name_locked(path, &pipe_idx);

  if (node && (flags & O_CREAT) && (flags & O_EXCL)) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = EEXIST;
    return -1;
  }

  if (!node) {
    // Allow creating on O_CREAT or when opened for writing
    int acc = flags & O_ACCMODE;
    if ((flags & O_CREAT) || acc == O_WRONLY || acc == O_RDWR) {
      pipe_idx = pipe_alloc_node_locked(path);
      if (pipe_idx < 0) {
        pthread_mutex_unlock(&pipe_table_lock);
        return -1;
      }
      node = &pipe_nodes[pipe_idx];
    } else {
      pthread_mutex_unlock(&pipe_table_lock);
      errno = ENOENT;
      return -1;
    }
  }

  int fd = pipe_alloc_fd_locked(pipe_idx, flags);
  if (fd < 0) {
    pipe_check_reclaim_locked(pipe_idx);
    pthread_mutex_unlock(&pipe_table_lock);
    return -1;
  }

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

  pthread_mutex_unlock(&pipe_table_lock);
  return fd;
}

static int pipefs_close(void *ctx, int fd) {
  (void)ctx;

  pthread_mutex_lock(&pipe_table_lock);
  if (fd < 0 || fd >= EOS_PIPEFS_MAX_FDS || !pipe_fds[fd].in_use) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = EBADF;
    return -1;
  }

  int pipe_idx = pipe_fds[fd].pipe_idx;
  int flags = pipe_fds[fd].flags;
  pipe_fds[fd].in_use = false;
  pipe_fds[fd].pipe_idx = -1;
  pipe_fds[fd].flags = 0;

  pipe_node_t *node = &pipe_nodes[pipe_idx];
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

  pipe_check_reclaim_locked(pipe_idx);
  pthread_mutex_unlock(&pipe_table_lock);
  return 0;
}

static ssize_t pipefs_read(void *ctx, int fd, void *dst, size_t size) {
  (void)ctx;
  if (!dst && size > 0) {
    errno = EINVAL;
    return -1;
  }
  if (size == 0)
    return 0;

  pthread_mutex_lock(&pipe_table_lock);
  if (fd < 0 || fd >= EOS_PIPEFS_MAX_FDS || !pipe_fds[fd].in_use) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = EBADF;
    return -1;
  }
  int pipe_idx = pipe_fds[fd].pipe_idx;
  int flags = pipe_fds[fd].flags;
  if ((flags & O_ACCMODE) == O_WRONLY) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = EBADF;
    return -1;
  }
  pipe_node_t *node = &pipe_nodes[pipe_idx];
  pthread_mutex_lock(&node->lock);
  pthread_mutex_unlock(&pipe_table_lock);

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

    // Buffer is empty: if no active writers remain, return EOF (0)
    if (node->writers == 0) {
      break;
    }

    if (flags & O_NONBLOCK) {
      pthread_mutex_unlock(&node->lock);
      errno = EAGAIN;
      return -1;
    }

    pthread_cond_wait(&node->r_cond, &node->lock);
  }

  pthread_mutex_unlock(&node->lock);

  if (total_read == 0) {
    pthread_mutex_lock(&pipe_table_lock);
    pipe_check_reclaim_locked(pipe_idx);
    pthread_mutex_unlock(&pipe_table_lock);
  }

  return (ssize_t)total_read;
}

static ssize_t pipefs_write(void *ctx, int fd, const void *data, size_t size) {
  (void)ctx;
  if (!data && size > 0) {
    errno = EINVAL;
    return -1;
  }
  if (size == 0)
    return 0;

  pthread_mutex_lock(&pipe_table_lock);
  if (fd < 0 || fd >= EOS_PIPEFS_MAX_FDS || !pipe_fds[fd].in_use) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = EBADF;
    return -1;
  }
  int pipe_idx = pipe_fds[fd].pipe_idx;
  int flags = pipe_fds[fd].flags;
  if ((flags & O_ACCMODE) == O_RDONLY) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = EBADF;
    return -1;
  }
  pipe_node_t *node = &pipe_nodes[pipe_idx];
  pthread_mutex_lock(&node->lock);
  pthread_mutex_unlock(&pipe_table_lock);

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

    pthread_cond_wait(&node->w_cond, &node->lock);
  }

  pthread_mutex_unlock(&node->lock);
  return (ssize_t)total_written;
}

static int pipefs_fstat(void *ctx, int fd, struct stat *st) {
  (void)ctx;
  if (!st) {
    errno = EINVAL;
    return -1;
  }

  pthread_mutex_lock(&pipe_table_lock);
  if (fd < 0 || fd >= EOS_PIPEFS_MAX_FDS || !pipe_fds[fd].in_use) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = EBADF;
    return -1;
  }
  pipe_node_t *node = &pipe_nodes[pipe_fds[fd].pipe_idx];
  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFIFO | 0666;
  st->st_size = (off_t)node->count;
  pthread_mutex_unlock(&pipe_table_lock);
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

  pthread_mutex_lock(&pipe_table_lock);
  pipe_node_t *node = pipe_find_by_name_locked(path, NULL);
  if (!node) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = ENOENT;
    return -1;
  }
  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFIFO | 0666;
  st->st_size = (off_t)node->count;
  pthread_mutex_unlock(&pipe_table_lock);
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

  pthread_mutex_lock(&pipe_table_lock);
  int idx = -1;
  pipe_node_t *node = pipe_find_by_name_locked(path, &idx);
  if (!node) {
    pthread_mutex_unlock(&pipe_table_lock);
    errno = ENOENT;
    return -1;
  }

  node->unlinked = true;
  pipe_check_reclaim_locked(idx);
  pthread_mutex_unlock(&pipe_table_lock);
  return 0;
}

static DIR *pipefs_opendir(void *ctx, const char *path) {
  (void)ctx;
  if (!path || (strcmp(path, "/") != 0 && strcmp(path, "") != 0)) {
    errno = ENOTDIR;
    return NULL;
  }

  pipe_dir_t *dir = calloc(1, sizeof(pipe_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }
  dir->idx = 0;
  return (DIR *)dir;
}

static struct dirent *pipefs_readdir(void *ctx, DIR *pdir) {
  (void)ctx;
  pipe_dir_t *dir = (pipe_dir_t *)pdir;
  if (!dir) {
    errno = EBADF;
    return NULL;
  }

  pthread_mutex_lock(&pipe_table_lock);
  while (dir->idx < EOS_PIPEFS_MAX_PIPES) {
    uint32_t i = dir->idx++;
    if (pipe_nodes[i].in_use && !pipe_nodes[i].unlinked) {
      memset(&dir->entry, 0, sizeof(dir->entry));
      dir->entry.d_ino = i;
      dir->entry.d_type = DT_FIFO;
      strlcpy(dir->entry.d_name, pipe_nodes[i].name, sizeof(dir->entry.d_name));
      pthread_mutex_unlock(&pipe_table_lock);
      return &dir->entry;
    }
  }
  pthread_mutex_unlock(&pipe_table_lock);
  return NULL;
}

static long pipefs_telldir(void *ctx, DIR *pdir) {
  (void)ctx;
  pipe_dir_t *dir = (pipe_dir_t *)pdir;
  if (!dir)
    return -1;
  return (long)dir->idx;
}

static void pipefs_seekdir(void *ctx, DIR *pdir, long offset) {
  (void)ctx;
  pipe_dir_t *dir = (pipe_dir_t *)pdir;
  if (!dir)
    return;
  dir->idx = (offset < 0) ? 0 : (uint32_t)offset;
}

static int pipefs_closedir(void *ctx, DIR *pdir) {
  (void)ctx;
  if (!pdir) {
    errno = EBADF;
    return -1;
  }
  free(pdir);
  return 0;
}

///////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////

int eos_pipefs_create(char *path_out, size_t len) {
  pthread_mutex_lock(&pipe_table_lock);
  int idx = pipe_alloc_node_locked(NULL);
  if (idx < 0) {
    pthread_mutex_unlock(&pipe_table_lock);
    return -1;
  }
  if (path_out && len > 0) {
    snprintf(path_out, len, EOS_PIPEFS_ROOT "/%s", pipe_nodes[idx].name);
  }
  pthread_mutex_unlock(&pipe_table_lock);
  return idx;
}

int pipe(int pipefd[2]) {
  if (!pipefd) {
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
  pthread_mutex_lock(&pipe_table_lock);
  pipe_nodes[idx].unlinked = true;
  pthread_mutex_unlock(&pipe_table_lock);

  pipefd[0] = rfd;
  pipefd[1] = wfd;
  return 0;
}

void eos_pipefs_init(void) {
  if (pipefs_initialized)
    return;

  for (int i = 0; i < EOS_PIPEFS_MAX_PIPES; i++) {
    memset(&pipe_nodes[i], 0, sizeof(pipe_node_t));
    pthread_mutex_init(&pipe_nodes[i].lock, NULL);
    pthread_cond_init(&pipe_nodes[i].r_cond, NULL);
    pthread_cond_init(&pipe_nodes[i].w_cond, NULL);
  }

  for (int i = 0; i < EOS_PIPEFS_MAX_FDS; i++) {
    memset(&pipe_fds[i], 0, sizeof(pipe_fd_t));
  }

  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,
      .open_p = pipefs_open,
      .close_p = pipefs_close,
      .read_p = pipefs_read,
      .write_p = pipefs_write,
      .stat_p = pipefs_stat,
      .fstat_p = pipefs_fstat,
      .unlink_p = pipefs_unlink,
      .opendir_p = pipefs_opendir,
      .readdir_p = pipefs_readdir,
      .telldir_p = pipefs_telldir,
      .seekdir_p = pipefs_seekdir,
      .closedir_p = pipefs_closedir,
  };

  mkdir(EOS_PIPEFS_ROOT, 0755);
  esp_vfs_register(EOS_PIPEFS_ROOT, &vfs, NULL);
  pipefs_initialized = true;
  EOS_LOGI("pipefs mounted at %s\n", EOS_PIPEFS_ROOT);
}
