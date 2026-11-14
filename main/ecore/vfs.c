#include "ecore/vfs.h"
#include "ecore/proc.h"
#include "emisc/kvec.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <string.h>

static kvec_psram_t(eos_vfs_file_t *) s_fd_descs;
static kvec_psram_t(eos_vfs_dir_t *) s_dir_descs;
static pthread_mutex_t s_vfs_table_lock;
static bool s_vfs_initialized = false;

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

void eos_vfs_init(void) {
  if (s_vfs_initialized) return;

  bool prev_h = eos_proc_heap_tracking_disable();

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&s_vfs_table_lock, &attr);
  pthread_mutexattr_destroy(&attr);

  kv_init(s_fd_descs);
  kv_init(s_dir_descs);

  // Pre-seed standard console file descriptors 0, 1, 2, 3 in PSRAM
  for (int i = 0; i < 4; i++) {
    eos_vfs_file_t *file = eos_psram_calloc_prefer(1, sizeof(eos_vfs_file_t));
    if (file) {
      file->fd = i;
      file->flags = 0;
      atomic_init(&file->ref_count, 1);
      atomic_init(&file->closing, false);
      pthread_mutex_init(&file->offset_lock, NULL);
      kv_push(eos_vfs_file_t *, s_fd_descs, file);
    }
  }

  s_vfs_initialized = true;
  eos_proc_heap_tracking_restore(prev_h);
}

// ── File Descriptor Operations ──────────────────────────────────

void eos_vfs_fd_register(int fd, int flags) {
  if (fd < 0) return;

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_vfs_file_t *file = eos_psram_calloc_prefer(1, sizeof(eos_vfs_file_t));
  if (!file) {
    eos_proc_heap_tracking_restore(prev_h);
    return;
  }

  file->fd = fd;
  file->flags = flags;
  atomic_init(&file->ref_count, 1); // 1 reference held by the central table
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
    // Last reference released, free file description
    pthread_mutex_destroy(&file->offset_lock);
    bool prev_h = eos_proc_heap_tracking_disable();
    eos_psram_free(file);
    eos_proc_heap_tracking_restore(prev_h);
  }
}

eos_vfs_file_t *eos_vfs_file_close_start(int fd) {
  if (fd < 0) return NULL;
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
  // Coarse per-fd locking across I/O is deprecated in favor of atomic refcounts
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

// ── Directory Stream Operations ─────────────────────────────────

void eos_vfs_dir_register(DIR *pdir) {
  if (!pdir) return;

  bool prev_h = eos_proc_heap_tracking_disable();
  eos_vfs_dir_t *dir = eos_psram_calloc_prefer(1, sizeof(eos_vfs_dir_t));
  if (!dir) {
    eos_proc_heap_tracking_restore(prev_h);
    return;
  }

  dir->pdir = pdir;
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
