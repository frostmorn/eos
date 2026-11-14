#include "ecore/vfs.h"
#include "ecore/fapi.h"
#include "ecore/log.h"
#include "ecore/proc.h"
#include "emisc/fancymacro.h"
#include "emisc/kvec.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <limits.h>
#include <reent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static kvec_psram_t(eos_vfs_mount_t *) s_mounts;
static kvec_psram_t(eos_vfs_file_t *) s_fd_descs;
static kvec_psram_t(eos_vfs_dir_t *) s_dir_descs;
static pthread_mutex_t s_vfs_table_lock;
static bool s_vfs_initialized = false;

// ── Locks ───────────────────────────────────────────────────────

void eos_vfs_alloc_lock(void) {
  if (!s_vfs_initialized) eos_vfs_init();
  pthread_mutex_lock(&s_vfs_table_lock);
}

void eos_vfs_alloc_unlock(void) {
  pthread_mutex_unlock(&s_vfs_table_lock);
}

void eos_vfs_table_lock(void) {
  eos_vfs_alloc_lock();
}

void eos_vfs_table_unlock(void) {
  eos_vfs_alloc_unlock();
}

// ── Subsystem Initialization ────────────────────────────────────

void eos_vfs_init(void) {
  if (s_vfs_initialized) return;

  bool prev_h = eos_proc_heap_tracking_disable();

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&s_vfs_table_lock, &attr);
  pthread_mutexattr_destroy(&attr);

  kv_init(s_mounts);
  kv_init(s_fd_descs);
  kv_init(s_dir_descs);

  // Pre-reserve capacities in PSRAM
  kv_resize(eos_vfs_mount_t *, s_mounts, 8);
  kv_resize(eos_vfs_file_t *, s_fd_descs, 64);
  kv_resize(eos_vfs_dir_t *, s_dir_descs, 16);

  // Pre-seed standard console file descriptors 0, 1, 2, 3 in PSRAM
  for (int i = 0; i < 4; i++) {
    eos_vfs_file_t *file = eos_psram_calloc_prefer(1, sizeof(eos_vfs_file_t));
    if (file) {
      file->fd = i;
      file->mount = NULL;
      file->flags = 0;
      atomic_init(&file->ref_count, 1);
      atomic_init(&file->closing, false);
      pthread_mutex_init(&file->offset_lock, NULL);
      kv_A(s_fd_descs, i) = file;
    }
  }

  s_vfs_initialized = true;
  eos_proc_heap_tracking_restore(prev_h);
}

// ── Allocation Helpers for Enlarged Structures ──────────────────

void *eos_vfs_file_alloc(const eos_vfs_mount_t *mount, size_t total_size, int flags) {
  if (!s_vfs_initialized) eos_vfs_init();
  if (total_size < sizeof(eos_vfs_file_t)) total_size = sizeof(eos_vfs_file_t);

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_vfs_file_t *file = eos_psram_calloc_prefer(1, total_size);
  if (!file) {
    eos_proc_heap_tracking_restore(prev_h);
    return NULL;
  }

  pthread_mutex_lock(&s_vfs_table_lock);

  // Allocate lowest unused FD >= 3
  int fd = -1;
  for (size_t i = 3; i < kv_size(s_fd_descs); i++) {
    if (kv_A(s_fd_descs, i) == NULL) {
      fd = (int)i;
      break;
    }
  }

  if (fd < 0) {
    fd = (int)kv_size(s_fd_descs);
    if (fd < 3) fd = 3;
    while (kv_size(s_fd_descs) <= (size_t)fd) {
      kv_push(eos_vfs_file_t *, s_fd_descs, NULL);
    }
  }

  file->fd = fd;
  file->mount = mount;
  file->flags = flags;
  atomic_init(&file->ref_count, 1);
  atomic_init(&file->closing, false);
  pthread_mutex_init(&file->offset_lock, NULL);

  kv_A(s_fd_descs, fd) = file;

  pthread_mutex_unlock(&s_vfs_table_lock);
  eos_proc_heap_tracking_restore(prev_h);

  return file;
}

void *eos_vfs_dir_alloc(const eos_vfs_mount_t *mount, size_t total_size) {
  if (!s_vfs_initialized) eos_vfs_init();
  if (total_size < sizeof(eos_vfs_dir_t)) total_size = sizeof(eos_vfs_dir_t);

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_vfs_dir_t *dir = eos_psram_calloc_prefer(1, total_size);
  if (!dir) {
    eos_proc_heap_tracking_restore(prev_h);
    return NULL;
  }

  dir->pdir = (DIR *)dir;
  dir->mount = mount;
  atomic_init(&dir->ref_count, 1);
  atomic_init(&dir->closing, false);
  pthread_mutex_init(&dir->lock, NULL);

  pthread_mutex_lock(&s_vfs_table_lock);
  bool placed = false;
  for (size_t i = 0; i < kv_size(s_dir_descs); i++) {
    if (kv_A(s_dir_descs, i) == NULL) {
      kv_A(s_dir_descs, i) = dir;
      placed = true;
      break;
    }
  }
  if (!placed) {
    kv_push(eos_vfs_dir_t *, s_dir_descs, dir);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  eos_proc_heap_tracking_restore(prev_h);

  return dir;
}

// ── File Descriptor Management ──────────────────────────────────

void eos_vfs_fd_register(int fd, int flags) {
  if (fd < 0) return;
  if (!s_vfs_initialized) eos_vfs_init();

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_vfs_file_t *file = eos_psram_calloc_prefer(1, sizeof(eos_vfs_file_t));
  if (!file) {
    eos_proc_heap_tracking_restore(prev_h);
    return;
  }

  file->fd = fd;
  file->mount = NULL;
  file->flags = flags;
  atomic_init(&file->ref_count, 1);
  atomic_init(&file->closing, false);
  pthread_mutex_init(&file->offset_lock, NULL);

  pthread_mutex_lock(&s_vfs_table_lock);
  while (kv_size(s_fd_descs) <= (size_t)fd) {
    kv_push(eos_vfs_file_t *, s_fd_descs, NULL);
  }

  eos_vfs_file_t *old_file = kv_A(s_fd_descs, fd);
  kv_A(s_fd_descs, fd) = file;
  pthread_mutex_unlock(&s_vfs_table_lock);
  eos_proc_heap_tracking_restore(prev_h);

  if (old_file) {
    eos_vfs_file_release(old_file);
  }
}

eos_vfs_file_t *eos_vfs_file_acquire(int fd) {
  if (fd < 0) return NULL;
  if (!s_vfs_initialized) eos_vfs_init();

  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd >= kv_size(s_fd_descs)) {
    pthread_mutex_unlock(&s_vfs_table_lock);
    return NULL;
  }
  eos_vfs_file_t *file = kv_A(s_fd_descs, fd);
  if (!file || atomic_load(&file->closing)) {
    pthread_mutex_unlock(&s_vfs_table_lock);
    return NULL;
  }
  atomic_fetch_add(&file->ref_count, 1);
  pthread_mutex_unlock(&s_vfs_table_lock);
  return file;
}

void eos_vfs_file_release(eos_vfs_file_t *file) {
  if (!file) return;
  if (atomic_fetch_sub(&file->ref_count, 1) == 1) {
    pthread_mutex_destroy(&file->offset_lock);
    bool prev_h = eos_proc_heap_tracking_disable();
    eos_psram_free(file);
    eos_proc_heap_tracking_restore(prev_h);
  }
}

eos_vfs_file_t *eos_vfs_file_close_start(int fd) {
  if (fd < 0) return NULL;
  if (!s_vfs_initialized) eos_vfs_init();

  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd >= kv_size(s_fd_descs)) {
    pthread_mutex_unlock(&s_vfs_table_lock);
    return NULL;
  }
  eos_vfs_file_t *file = kv_A(s_fd_descs, fd);
  if (!file || atomic_load(&file->closing)) {
    pthread_mutex_unlock(&s_vfs_table_lock);
    return NULL;
  }
  atomic_store(&file->closing, true);
  kv_A(s_fd_descs, fd) = NULL;
  pthread_mutex_unlock(&s_vfs_table_lock);
  return file;
}

bool eos_vfs_fd_is_valid(int fd) {
  if (fd < 0) return false;
  if (!s_vfs_initialized) eos_vfs_init();

  bool valid = false;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs)) {
    eos_vfs_file_t *file = kv_A(s_fd_descs, fd);
    valid = (file != NULL && !atomic_load(&file->closing));
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  return valid;
}

int eos_vfs_fd_ref(int fd) {
  eos_vfs_file_t *file = eos_vfs_file_acquire(fd);
  if (file) {
    int count = atomic_load(&file->ref_count);
    return count;
  }
  return 1;
}

int eos_vfs_fd_unref(int fd) {
  if (fd < 0) return 0;
  eos_vfs_file_t *file = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs)) {
    file = kv_A(s_fd_descs, fd);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (file) {
    int count = atomic_load(&file->ref_count) - 1;
    eos_vfs_file_release(file);
    return count;
  }
  return 0;
}

void eos_vfs_fd_lock(int fd) {
  (void)fd;
}

void eos_vfs_fd_unlock(int fd) {
  (void)fd;
}

void eos_vfs_fd_unregister(int fd) {
  eos_vfs_file_t *file = eos_vfs_file_close_start(fd);
  if (file) {
    eos_vfs_file_release(file);
  }
}

// ── Directory Stream Management ─────────────────────────────────

void eos_vfs_dir_register(DIR *pdir) {
  if (!pdir) return;
  if (!s_vfs_initialized) eos_vfs_init();

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_vfs_dir_t *dir = eos_psram_calloc_prefer(1, sizeof(eos_vfs_dir_t));
  if (!dir) {
    eos_proc_heap_tracking_restore(prev_h);
    return;
  }

  dir->pdir = pdir;
  dir->mount = NULL;
  atomic_init(&dir->ref_count, 1);
  atomic_init(&dir->closing, false);
  pthread_mutex_init(&dir->lock, NULL);

  pthread_mutex_lock(&s_vfs_table_lock);
  kv_push(eos_vfs_dir_t *, s_dir_descs, dir);
  pthread_mutex_unlock(&s_vfs_table_lock);
  eos_proc_heap_tracking_restore(prev_h);
}

eos_vfs_dir_t *eos_vfs_dir_acquire(DIR *pdir) {
  if (!pdir) return NULL;
  if (!s_vfs_initialized) eos_vfs_init();

  pthread_mutex_lock(&s_vfs_table_lock);
  for (size_t i = 0; i < kv_size(s_dir_descs); i++) {
    eos_vfs_dir_t *d = kv_A(s_dir_descs, i);
    if (d && d->pdir == pdir && !atomic_load(&d->closing)) {
      atomic_fetch_add(&d->ref_count, 1);
      pthread_mutex_unlock(&s_vfs_table_lock);
      return d;
    }
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  return NULL;
}

void eos_vfs_dir_release(eos_vfs_dir_t *dir) {
  if (!dir) return;
  if (atomic_fetch_sub(&dir->ref_count, 1) == 1) {
    pthread_mutex_destroy(&dir->lock);
    bool prev_h = eos_proc_heap_tracking_disable();
    eos_psram_free(dir);
    eos_proc_heap_tracking_restore(prev_h);
  }
}

eos_vfs_dir_t *eos_vfs_dir_close_start(DIR *pdir) {
  if (!pdir) return NULL;
  if (!s_vfs_initialized) eos_vfs_init();

  pthread_mutex_lock(&s_vfs_table_lock);
  for (size_t i = 0; i < kv_size(s_dir_descs); i++) {
    eos_vfs_dir_t *d = kv_A(s_dir_descs, i);
    if (d && d->pdir == pdir && !atomic_load(&d->closing)) {
      atomic_store(&d->closing, true);
      kv_A(s_dir_descs, i) = NULL;
      pthread_mutex_unlock(&s_vfs_table_lock);
      return d;
    }
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  return NULL;
}

void eos_vfs_dir_lock(DIR *pdir) {
  (void)pdir;
}

void eos_vfs_dir_unlock(DIR *pdir) {
  (void)pdir;
}

void eos_vfs_dir_close(DIR *pdir) {
  eos_vfs_dir_t *dir = eos_vfs_dir_close_start(pdir);
  if (dir) {
    eos_vfs_dir_release(dir);
  }
}

// ── Mount Registration & Lookup ─────────────────────────────────

esp_err_t eos_vfs_register_ops(const char *base_path, const eos_vfs_ops_t *ops, void *ctx) {
  if (!base_path || !ops) return ESP_ERR_INVALID_ARG;
  if (!s_vfs_initialized) eos_vfs_init();

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_vfs_mount_t *mount = eos_psram_calloc_prefer(1, sizeof(eos_vfs_mount_t));
  if (!mount) {
    eos_proc_heap_tracking_restore(prev_h);
    return ESP_ERR_NO_MEM;
  }

  strlcpy(mount->prefix, base_path, sizeof(mount->prefix));
  // Clean trailing slash unless root
  size_t len = strlen(mount->prefix);
  while (len > 1 && mount->prefix[len - 1] == '/') {
    mount->prefix[len - 1] = '\0';
    len--;
  }
  mount->prefix_len = len;
  mount->ops = *ops;
  mount->ctx = ctx;
  mount->in_use = true;

  pthread_mutex_lock(&s_vfs_table_lock);
  // Check if replacing an existing mount
  bool replaced = false;
  for (size_t i = 0; i < kv_size(s_mounts); i++) {
    eos_vfs_mount_t *m = kv_A(s_mounts, i);
    if (m && strcmp(m->prefix, mount->prefix) == 0) {
      kv_A(s_mounts, i) = mount;
      eos_psram_free(m);
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    kv_push(eos_vfs_mount_t *, s_mounts, mount);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  eos_proc_heap_tracking_restore(prev_h);

  EOS_LOGI("Registered VFS mount %s\n", mount->prefix);
  return ESP_OK;
}

// Adapters for classic esp_vfs_t
typedef struct {
  esp_vfs_t vfs;
  void *ctx;
} esp_vfs_compat_t;

static int esp_compat_open(void *ctx, const char *path, int flags, int mode, eos_vfs_file_t **out_file) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  int lfd = -1;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    lfd = c->vfs.open_p(c->ctx, path, flags, mode);
  } else if (c->vfs.open) {
    lfd = c->vfs.open(path, flags, mode);
  }
  if (lfd < 0) return -1;

  eos_vfs_file_t *file = eos_vfs_file_alloc(NULL, sizeof(eos_vfs_file_t), flags);
  if (!file) {
    if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) c->vfs.close_p(c->ctx, lfd);
    else if (c->vfs.close) c->vfs.close(lfd);
    errno = ENOMEM;
    return -1;
  }
  file->fs_data = (void *)(intptr_t)lfd;
  *out_file = file;
  return file->fd;
}

static int esp_compat_close(void *ctx, eos_vfs_file_t *file) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  int lfd = (int)(intptr_t)file->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.close_p(c->ctx, lfd);
  } else if (c->vfs.close) {
    return c->vfs.close(lfd);
  }
  return 0;
}

static ssize_t esp_compat_read(void *ctx, eos_vfs_file_t *file, void *dst, size_t size) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  int lfd = (int)(intptr_t)file->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.read_p(c->ctx, lfd, dst, size);
  } else if (c->vfs.read) {
    return c->vfs.read(lfd, dst, size);
  }
  errno = ENOSYS;
  return -1;
}

static ssize_t esp_compat_write(void *ctx, eos_vfs_file_t *file, const void *src, size_t size) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  int lfd = (int)(intptr_t)file->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.write_p(c->ctx, lfd, src, size);
  } else if (c->vfs.write) {
    return c->vfs.write(lfd, src, size);
  }
  errno = ENOSYS;
  return -1;
}

static off_t esp_compat_lseek(void *ctx, eos_vfs_file_t *file, off_t offset, int whence) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  int lfd = (int)(intptr_t)file->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.lseek_p(c->ctx, lfd, offset, whence);
  } else if (c->vfs.lseek) {
    return c->vfs.lseek(lfd, offset, whence);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_fstat(void *ctx, eos_vfs_file_t *file, struct stat *st) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  int lfd = (int)(intptr_t)file->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.fstat_p(c->ctx, lfd, st);
  } else if (c->vfs.fstat) {
    return c->vfs.fstat(lfd, st);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_stat(void *ctx, const char *path, struct stat *st) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.stat_p(c->ctx, path, st);
  } else if (c->vfs.stat) {
    return c->vfs.stat(path, st);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_link(void *ctx, const char *oldpath, const char *newpath) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.link_p ? c->vfs.link_p(c->ctx, oldpath, newpath) : -1;
  } else if (c->vfs.link) {
    return c->vfs.link(oldpath, newpath);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_unlink(void *ctx, const char *path) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.unlink_p ? c->vfs.unlink_p(c->ctx, path) : -1;
  } else if (c->vfs.unlink) {
    return c->vfs.unlink(path);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_rename(void *ctx, const char *src, const char *dst) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.rename_p ? c->vfs.rename_p(c->ctx, src, dst) : -1;
  } else if (c->vfs.rename) {
    return c->vfs.rename(src, dst);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_mkdir(void *ctx, const char *path, mode_t mode) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.mkdir_p ? c->vfs.mkdir_p(c->ctx, path, mode) : -1;
  } else if (c->vfs.mkdir) {
    return c->vfs.mkdir(path, mode);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_rmdir(void *ctx, const char *path) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.rmdir_p ? c->vfs.rmdir_p(c->ctx, path) : -1;
  } else if (c->vfs.rmdir) {
    return c->vfs.rmdir(path);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_access(void *ctx, const char *path, int amode) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    if (c->vfs.access_p) return c->vfs.access_p(c->ctx, path, amode);
  } else if (c->vfs.access) {
    return c->vfs.access(path, amode);
  }
  struct stat st;
  if (esp_compat_stat(ctx, path, &st) == 0) return 0;
  return -1;
}

static int esp_compat_ioctl(void *ctx, eos_vfs_file_t *file, int cmd, va_list args) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  int lfd = (int)(intptr_t)file->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.ioctl_p ? c->vfs.ioctl_p(c->ctx, lfd, cmd, args) : -1;
  } else if (c->vfs.ioctl) {
    return c->vfs.ioctl(lfd, cmd, args);
  }
  errno = ENOSYS;
  return -1;
}

static int esp_compat_opendir(void *ctx, const char *name, eos_vfs_dir_t **out_dir) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  DIR *ldir = NULL;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    ldir = c->vfs.opendir_p ? c->vfs.opendir_p(c->ctx, name) : NULL;
  } else if (c->vfs.opendir) {
    ldir = c->vfs.opendir(name);
  }
  if (!ldir) return -1;

  eos_vfs_dir_t *dir = eos_vfs_dir_alloc(NULL, sizeof(eos_vfs_dir_t));
  if (!dir) {
    if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) c->vfs.closedir_p(c->ctx, ldir);
    else if (c->vfs.closedir) c->vfs.closedir(ldir);
    errno = ENOMEM;
    return -1;
  }
  dir->fs_data = ldir;
  *out_dir = dir;
  return 0;
}

static struct dirent *esp_compat_readdir(void *ctx, eos_vfs_dir_t *dir) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  DIR *ldir = (DIR *)dir->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.readdir_p ? c->vfs.readdir_p(c->ctx, ldir) : NULL;
  } else if (c->vfs.readdir) {
    return c->vfs.readdir(ldir);
  }
  return NULL;
}

static int esp_compat_closedir(void *ctx, eos_vfs_dir_t *dir) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  DIR *ldir = (DIR *)dir->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.closedir_p ? c->vfs.closedir_p(c->ctx, ldir) : 0;
  } else if (c->vfs.closedir) {
    return c->vfs.closedir(ldir);
  }
  return 0;
}

static void esp_compat_seekdir(void *ctx, eos_vfs_dir_t *dir, long loc) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  DIR *ldir = (DIR *)dir->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    if (c->vfs.seekdir_p) c->vfs.seekdir_p(c->ctx, ldir, loc);
  } else if (c->vfs.seekdir) {
    c->vfs.seekdir(ldir, loc);
  }
}

static long esp_compat_telldir(void *ctx, eos_vfs_dir_t *dir) {
  esp_vfs_compat_t *c = (esp_vfs_compat_t *)ctx;
  DIR *ldir = (DIR *)dir->fs_data;
  if (c->vfs.flags & ESP_VFS_FLAG_CONTEXT_PTR) {
    return c->vfs.telldir_p ? c->vfs.telldir_p(c->ctx, ldir) : -1;
  } else if (c->vfs.telldir) {
    return c->vfs.telldir(ldir);
  }
  return -1;
}

esp_err_t eos_vfs_register(const char *base_path, const esp_vfs_t *vfs, void *ctx) {
  if (!base_path || !vfs) return ESP_ERR_INVALID_ARG;
  bool prev_h = eos_proc_heap_tracking_disable();
  esp_vfs_compat_t *c = eos_psram_calloc_prefer(1, sizeof(esp_vfs_compat_t));
  if (!c) {
    eos_proc_heap_tracking_restore(prev_h);
    return ESP_ERR_NO_MEM;
  }
  c->vfs = *vfs;
  c->ctx = ctx;

  eos_vfs_ops_t ops = {
      .open = esp_compat_open,
      .close = esp_compat_close,
      .read = esp_compat_read,
      .write = esp_compat_write,
      .lseek = esp_compat_lseek,
      .fstat = esp_compat_fstat,
      .stat = esp_compat_stat,
      .link = esp_compat_link,
      .unlink = esp_compat_unlink,
      .rename = esp_compat_rename,
      .mkdir = esp_compat_mkdir,
      .rmdir = esp_compat_rmdir,
      .access = esp_compat_access,
      .ioctl = esp_compat_ioctl,
      .opendir = esp_compat_opendir,
      .readdir = esp_compat_readdir,
      .closedir = esp_compat_closedir,
      .seekdir = esp_compat_seekdir,
      .telldir = esp_compat_telldir,
      .symlink = NULL,
      .readlink = NULL,
      .lstat = NULL,
  };
  eos_proc_heap_tracking_restore(prev_h);
  return eos_vfs_register_ops(base_path, &ops, c);
}

esp_err_t eos_vfs_unregister(const char *base_path) {
  if (!base_path) return ESP_ERR_INVALID_ARG;
  pthread_mutex_lock(&s_vfs_table_lock);
  for (size_t i = 0; i < kv_size(s_mounts); i++) {
    eos_vfs_mount_t *m = kv_A(s_mounts, i);
    if (m && m->in_use && strcmp(m->prefix, base_path) == 0) {
      m->in_use = false;
      pthread_mutex_unlock(&s_vfs_table_lock);
      return ESP_OK;
    }
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  return ESP_ERR_NOT_FOUND;
}

const eos_vfs_mount_t *eos_vfs_match_mount(const char *path, const char **out_rel_path) {
  if (!path) return NULL;
  if (!s_vfs_initialized) eos_vfs_init();

  pthread_mutex_lock(&s_vfs_table_lock);
  const eos_vfs_mount_t *best_mount = NULL;
  const eos_vfs_mount_t *root_mount = NULL;
  size_t best_len = 0;
  size_t path_len = strlen(path);

  for (size_t i = 0; i < kv_size(s_mounts); i++) {
    eos_vfs_mount_t *m = kv_A(s_mounts, i);
    if (!m || !m->in_use) continue;

    if (m->prefix_len == 0 || (m->prefix_len == 1 && m->prefix[0] == '/')) {
      root_mount = m;
      continue;
    }

    if (path_len >= m->prefix_len &&
        strncmp(path, m->prefix, m->prefix_len) == 0) {
      if (path_len == m->prefix_len || path[m->prefix_len] == '/') {
        if (m->prefix_len > best_len) {
          best_mount = m;
          best_len = m->prefix_len;
        }
      }
    }
  }

  if (!best_mount) {
    best_mount = root_mount;
  }

  if (best_mount && out_rel_path) {
    if (best_mount == root_mount) {
      *out_rel_path = path;
    } else if (path_len == best_mount->prefix_len) {
      *out_rel_path = "/";
    } else {
      *out_rel_path = path + best_mount->prefix_len;
    }
  }

  pthread_mutex_unlock(&s_vfs_table_lock);
  return best_mount;
}

// ── Symlink Resolution ──────────────────────────────────────────

int eos_vfs_resolve_symlinks(const char *path, char *out_buf, size_t out_size) {
  if (!path || !out_buf || out_size == 0) {
    errno = EINVAL;
    return -1;
  }

  char current[PATH_MAX];
  char resolved[PATH_MAX];
  strlcpy(current, path, sizeof(current));
  eos_fapi_path_normalize(current);

  int loop_count = 0;
  const int max_loops = 8;

  while (loop_count < max_loops) {
    const char *rel_path = NULL;
    const eos_vfs_mount_t *mount = eos_vfs_match_mount(current, &rel_path);
    if (!mount || !mount->ops.readlink) {
      // Mount doesn't support symlinks, current path is final
      strlcpy(out_buf, current, out_size);
      return 0;
    }

    char target[PATH_MAX];
    ssize_t len = mount->ops.readlink(mount->ctx, rel_path, target, sizeof(target) - 1);
    if (len < 0) {
      // Not a symlink (or error like ENOENT/EINVAL), current path is final
      strlcpy(out_buf, current, out_size);
      return 0;
    }
    target[len] = '\0';
    loop_count++;

    // Follow symlink
    if (target[0] == '/') {
      strlcpy(resolved, target, sizeof(resolved));
    } else {
      // Relative to parent directory of current symlink
      char *last_slash = strrchr(current, '/');
      if (last_slash && last_slash != current) {
        *last_slash = '\0';
        strlcpy(resolved, current, sizeof(resolved));
        strlcat(resolved, "/", sizeof(resolved));
        strlcat(resolved, target, sizeof(resolved));
      } else {
        strlcpy(resolved, "/", sizeof(resolved));
        strlcat(resolved, target, sizeof(resolved));
      }
    }
    eos_fapi_path_normalize(resolved);
    strlcpy(current, resolved, sizeof(current));
  }

  errno = ELOOP;
  return -1;
}

// ── Centralized VFS Dispatch Operations ─────────────────────────

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

int eos_vfs_open_r(struct _reent *r, const char *path, int flags, int mode) {
  char resolved_path[PATH_MAX];
  if (eos_vfs_resolve_symlinks(path, resolved_path, sizeof(resolved_path)) != 0 && errno == ELOOP) {
    __errno_r(r) = ELOOP;
    return -1;
  }

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved_path, &rel_path);
  if (mount && mount->ops.open) {
    eos_vfs_file_t *file = NULL;
    int fd = mount->ops.open(mount->ctx, rel_path, flags, mode, &file);
    if (fd >= 0 && file) {
      file->mount = mount;
      return fd;
    }
    return -1;
  }

  // Fallback to real open
  bool prev_h = eos_proc_heap_tracking_disable();
  int fd = __real__open_r(r, resolved_path, flags, mode);
  eos_proc_heap_tracking_restore(prev_h);
  if (fd >= 0) {
    eos_vfs_fd_register(fd, flags);
  }
  return fd;
}

int eos_vfs_close_r(struct _reent *r, int fd) {
  eos_vfs_file_t *file = eos_vfs_file_close_start(fd);
  if (file && file->mount) {
    int ret = 0;
    if (file->mount->ops.close) {
      ret = file->mount->ops.close(file->mount->ctx, file);
    }
    eos_vfs_file_release(file);
    return ret;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__close_r(r, fd);
  eos_proc_heap_tracking_restore(prev_h);
  if (file) {
    eos_vfs_file_release(file);
  }
  return ret;
}

ssize_t eos_vfs_read_r(struct _reent *r, int fd, void *dst, size_t size) {
  if (fd < 0) {
    __errno_r(r) = EBADF;
    return -1;
  }
  eos_vfs_file_t *file = eos_vfs_file_acquire(fd);
  if (file && file->mount) {
    if (!file->mount->ops.read) {
      __errno_r(r) = ENOSYS;
      eos_vfs_file_release(file);
      return -1;
    }
    ssize_t ret = file->mount->ops.read(file->mount->ctx, file, dst, size);
    eos_vfs_file_release(file);
    return ret;
  }

  ssize_t ret = __real__read_r(r, fd, dst, size);
  if (file) eos_vfs_file_release(file);
  return ret;
}

ssize_t eos_vfs_write_r(struct _reent *r, int fd, const void *src, size_t size) {
  if (fd < 0) {
    __errno_r(r) = EBADF;
    return -1;
  }
  eos_vfs_file_t *file = eos_vfs_file_acquire(fd);
  if (file && file->mount) {
    if (!file->mount->ops.write) {
      __errno_r(r) = ENOSYS;
      eos_vfs_file_release(file);
      return -1;
    }
    ssize_t ret = file->mount->ops.write(file->mount->ctx, file, src, size);
    eos_vfs_file_release(file);
    return ret;
  }

  ssize_t ret = __real__write_r(r, fd, (void *)src, size);
  if (file) eos_vfs_file_release(file);
  return ret;
}

off_t eos_vfs_lseek_r(struct _reent *r, int fd, off_t offset, int whence) {
  if (fd < 0) {
    __errno_r(r) = EBADF;
    return -1;
  }
  eos_vfs_file_t *file = eos_vfs_file_acquire(fd);
  if (file && file->mount) {
    if (!file->mount->ops.lseek) {
      __errno_r(r) = ESPIPE;
      eos_vfs_file_release(file);
      return -1;
    }
    pthread_mutex_lock(&file->offset_lock);
    off_t ret = file->mount->ops.lseek(file->mount->ctx, file, offset, whence);
    pthread_mutex_unlock(&file->offset_lock);
    eos_vfs_file_release(file);
    return ret;
  }

  off_t ret = __real__lseek_r(r, fd, offset, whence);
  if (file) eos_vfs_file_release(file);
  return ret;
}

int eos_vfs_fstat_r(struct _reent *r, int fd, struct stat *st) {
  if (fd < 0) {
    __errno_r(r) = EBADF;
    return -1;
  }
  eos_vfs_file_t *file = eos_vfs_file_acquire(fd);
  if (file && file->mount) {
    if (!file->mount->ops.fstat) {
      __errno_r(r) = ENOSYS;
      eos_vfs_file_release(file);
      return -1;
    }
    int ret = file->mount->ops.fstat(file->mount->ctx, file, st);
    eos_vfs_file_release(file);
    return ret;
  }

  int ret = __real__fstat_r(r, fd, st);
  if (file) eos_vfs_file_release(file);
  return ret;
}

int eos_vfs_stat_r(struct _reent *r, const char *path, struct stat *st) {
  char resolved_path[PATH_MAX];
  if (eos_vfs_resolve_symlinks(path, resolved_path, sizeof(resolved_path)) != 0 && errno == ELOOP) {
    __errno_r(r) = ELOOP;
    return -1;
  }

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved_path, &rel_path);
  if (mount && mount->ops.stat) {
    return mount->ops.stat(mount->ctx, rel_path, st);
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__stat_r(r, resolved_path, st);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

int eos_vfs_link_r(struct _reent *r, const char *oldpath, const char *newpath) {
  char r_old[PATH_MAX], r_new[PATH_MAX];
  if (eos_vfs_resolve_symlinks(oldpath, r_old, sizeof(r_old)) != 0 && errno == ELOOP) {
    __errno_r(r) = ELOOP;
    return -1;
  }
  strlcpy(r_new, newpath, sizeof(r_new));
  eos_fapi_path_normalize(r_new);

  const char *rel_old = NULL, *rel_new = NULL;
  const eos_vfs_mount_t *m1 = eos_vfs_match_mount(r_old, &rel_old);
  const eos_vfs_mount_t *m2 = eos_vfs_match_mount(r_new, &rel_new);

  if (m1 != m2) {
    __errno_r(r) = EXDEV;
    return -1;
  }

  if (m1 && m1->ops.link) {
    return m1->ops.link(m1->ctx, rel_old, rel_new);
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__link_r(r, r_old, r_new);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

int eos_vfs_unlink_r(struct _reent *r, const char *path) {
  char resolved_path[PATH_MAX];
  strlcpy(resolved_path, path, sizeof(resolved_path));
  eos_fapi_path_normalize(resolved_path);

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved_path, &rel_path);
  if (mount && mount->ops.unlink) {
    return mount->ops.unlink(mount->ctx, rel_path);
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__unlink_r(r, resolved_path);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

int eos_vfs_rename_r(struct _reent *r, const char *src, const char *dst) {
  char r_src[PATH_MAX], r_dst[PATH_MAX];
  strlcpy(r_src, src, sizeof(r_src));
  strlcpy(r_dst, dst, sizeof(r_dst));
  eos_fapi_path_normalize(r_src);
  eos_fapi_path_normalize(r_dst);

  const char *rel_src = NULL, *rel_dst = NULL;
  const eos_vfs_mount_t *m_src = eos_vfs_match_mount(r_src, &rel_src);
  const eos_vfs_mount_t *m_dst = eos_vfs_match_mount(r_dst, &rel_dst);

  if (m_src != m_dst) {
    __errno_r(r) = EXDEV;
    return -1;
  }

  if (m_src && m_src->ops.rename) {
    return m_src->ops.rename(m_src->ctx, rel_src, rel_dst);
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real__rename_r(r, r_src, r_dst);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

DIR *eos_vfs_opendir(const char *name) {
  char resolved_path[PATH_MAX];
  if (eos_vfs_resolve_symlinks(name, resolved_path, sizeof(resolved_path)) != 0 && errno == ELOOP) {
    errno = ELOOP;
    return NULL;
  }

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved_path, &rel_path);
  if (mount && mount->ops.opendir) {
    eos_vfs_dir_t *dir = NULL;
    int ret = mount->ops.opendir(mount->ctx, rel_path, &dir);
    if (ret == 0 && dir) {
      dir->mount = mount;
      return (DIR *)dir;
    }
    return NULL;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  DIR *pdir = __real_opendir(resolved_path);
  eos_proc_heap_tracking_restore(prev_h);
  if (pdir) {
    eos_vfs_dir_register(pdir);
  }
  return pdir;
}

struct dirent *eos_vfs_readdir(DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return NULL;
  }
  eos_vfs_dir_t *dir = eos_vfs_dir_acquire(pdir);
  if (dir && dir->mount) {
    if (!dir->mount->ops.readdir) {
      errno = ENOSYS;
      eos_vfs_dir_release(dir);
      return NULL;
    }
    pthread_mutex_lock(&dir->lock);
    struct dirent *ent = dir->mount->ops.readdir(dir->mount->ctx, dir);
    pthread_mutex_unlock(&dir->lock);
    eos_vfs_dir_release(dir);
    return ent;
  }

  if (dir) {
    pthread_mutex_lock(&dir->lock);
  }
  bool prev_h = eos_proc_heap_tracking_disable();
  struct dirent *ent = __real_readdir(pdir);
  eos_proc_heap_tracking_restore(prev_h);
  if (dir) {
    pthread_mutex_unlock(&dir->lock);
    eos_vfs_dir_release(dir);
  }
  return ent;
}

int eos_vfs_closedir(DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return -1;
  }
  eos_vfs_dir_t *dir = eos_vfs_dir_close_start(pdir);
  if (dir && dir->mount) {
    int ret = 0;
    if (dir->mount->ops.closedir) {
      ret = dir->mount->ops.closedir(dir->mount->ctx, dir);
    }
    eos_vfs_dir_release(dir);
    return ret;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real_closedir(pdir);
  eos_proc_heap_tracking_restore(prev_h);
  if (dir) {
    eos_vfs_dir_release(dir);
  }
  return ret;
}

void eos_vfs_seekdir(DIR *pdir, long loc) {
  if (!pdir) {
    errno = EBADF;
    return;
  }
  eos_vfs_dir_t *dir = eos_vfs_dir_acquire(pdir);
  if (dir && dir->mount) {
    if (dir->mount->ops.seekdir) {
      pthread_mutex_lock(&dir->lock);
      dir->mount->ops.seekdir(dir->mount->ctx, dir, loc);
      pthread_mutex_unlock(&dir->lock);
    }
    eos_vfs_dir_release(dir);
    return;
  }
  if (dir) {
    eos_vfs_dir_release(dir);
  }
  bool prev_h = eos_proc_heap_tracking_disable();
  __real_seekdir(pdir, loc);
  eos_proc_heap_tracking_restore(prev_h);
}

long eos_vfs_telldir(DIR *pdir) {
  if (!pdir) {
    errno = EBADF;
    return -1;
  }
  eos_vfs_dir_t *dir = eos_vfs_dir_acquire(pdir);
  if (dir && dir->mount) {
    long ret = -1;
    if (dir->mount->ops.telldir) {
      pthread_mutex_lock(&dir->lock);
      ret = dir->mount->ops.telldir(dir->mount->ctx, dir);
      pthread_mutex_unlock(&dir->lock);
    }
    eos_vfs_dir_release(dir);
    return ret;
  }
  if (dir) {
    eos_vfs_dir_release(dir);
  }
  bool prev_h = eos_proc_heap_tracking_disable();
  long ret = __real_telldir(pdir);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

void eos_vfs_rewinddir(DIR *pdir) {
  eos_vfs_seekdir(pdir, 0);
}

int eos_vfs_mkdir(const char *path, mode_t mode) {
  char resolved_path[PATH_MAX];
  strlcpy(resolved_path, path, sizeof(resolved_path));
  eos_fapi_path_normalize(resolved_path);

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved_path, &rel_path);
  if (mount && mount->ops.mkdir) {
    return mount->ops.mkdir(mount->ctx, rel_path, mode);
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real_mkdir(resolved_path, mode);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

int eos_vfs_rmdir(const char *path) {
  char resolved_path[PATH_MAX];
  strlcpy(resolved_path, path, sizeof(resolved_path));
  eos_fapi_path_normalize(resolved_path);

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved_path, &rel_path);
  if (mount && mount->ops.rmdir) {
    return mount->ops.rmdir(mount->ctx, rel_path);
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real_rmdir(resolved_path);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

int eos_vfs_access(const char *path, int amode) {
  char resolved_path[PATH_MAX];
  if (eos_vfs_resolve_symlinks(path, resolved_path, sizeof(resolved_path)) != 0 && errno == ELOOP) {
    errno = ELOOP;
    return -1;
  }

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved_path, &rel_path);
  if (mount) {
    if (mount->ops.access) {
      return mount->ops.access(mount->ctx, rel_path, amode);
    }
    struct stat st;
    if (mount->ops.stat && mount->ops.stat(mount->ctx, rel_path, &st) == 0) {
      return 0;
    }
    return -1;
  }

  bool prev_h = eos_proc_heap_tracking_disable();
  int ret = __real_access(resolved_path, amode);
  eos_proc_heap_tracking_restore(prev_h);
  return ret;
}

int eos_vfs_ioctl(int fd, int cmd, va_list args) {
  if (fd < 0) {
    errno = EBADF;
    return -1;
  }
  eos_vfs_file_t *file = eos_vfs_file_acquire(fd);
  if (file && file->mount) {
    if (!file->mount->ops.ioctl) {
      errno = ENOSYS;
      eos_vfs_file_release(file);
      return -1;
    }
    int ret = file->mount->ops.ioctl(file->mount->ctx, file, cmd, args);
    eos_vfs_file_release(file);
    return ret;
  }

  void *arg = va_arg(args, void *);
  int ret = __real_ioctl(fd, cmd, arg);
  if (file) eos_vfs_file_release(file);
  return ret;
}

int eos_vfs_symlink(const char *target, const char *linkpath) {
  if (!target || !linkpath) {
    errno = EINVAL;
    return -1;
  }
  char resolved[PATH_MAX];
  strlcpy(resolved, linkpath, sizeof(resolved));
  eos_fapi_path_normalize(resolved);

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved, &rel_path);
  if (mount && mount->ops.symlink) {
    return mount->ops.symlink(mount->ctx, target, rel_path);
  }
  errno = ENOSYS;
  return -1;
}

ssize_t eos_vfs_readlink(const char *path, char *buf, size_t bufsiz) {
  if (!path || !buf || bufsiz == 0) {
    errno = EINVAL;
    return -1;
  }
  char resolved[PATH_MAX];
  strlcpy(resolved, path, sizeof(resolved));
  eos_fapi_path_normalize(resolved);

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved, &rel_path);
  if (mount && mount->ops.readlink) {
    return mount->ops.readlink(mount->ctx, rel_path, buf, bufsiz);
  }
  errno = EINVAL;
  return -1;
}

int eos_vfs_lstat(const char *path, struct stat *st) {
  if (!path || !st) {
    errno = EINVAL;
    return -1;
  }
  char resolved[PATH_MAX];
  strlcpy(resolved, path, sizeof(resolved));
  eos_fapi_path_normalize(resolved);

  const char *rel_path = NULL;
  const eos_vfs_mount_t *mount = eos_vfs_match_mount(resolved, &rel_path);
  if (mount) {
    if (mount->ops.lstat) {
      return mount->ops.lstat(mount->ctx, rel_path, st);
    }
    if (mount->ops.stat) {
      return mount->ops.stat(mount->ctx, rel_path, st);
    }
  }
  return __real__stat_r(__getreent(), resolved, st);
}

void eos_vfs_dump_fds(FILE *f) {
  if (!f) return;
  pthread_mutex_lock(&s_vfs_table_lock);
  fprintf(f, "FD\tMount\tRefs\n");
  for (size_t i = 0; i < kv_size(s_fd_descs); i++) {
    eos_vfs_file_t *file = kv_A(s_fd_descs, i);
    if (file && !atomic_load(&file->closing)) {
      fprintf(f, "%d\t%s\t%d\n",
              file->fd,
              file->mount ? file->mount->prefix : "<system>",
              atomic_load(&file->ref_count));
    }
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
}
