#include "ecore/vfs.h"
#include "ecore/proc.h"
#include "emisc/kvec.h"
#include "emisc/psram_alloc.h"
#include <errno.h>
#include <string.h>

static kvec_psram_t(eos_vfs_fd_desc_t *) s_fd_descs;
static kvec_psram_t(eos_vfs_dir_desc_t *) s_dir_descs;
static pthread_mutex_t s_vfs_table_lock;
static bool s_vfs_initialized = false;

void eos_vfs_alloc_lock(void) {
  if (!s_vfs_initialized) eos_vfs_init();
  pthread_mutex_lock(&s_vfs_table_lock);
}

void eos_vfs_alloc_unlock(void) {
  pthread_mutex_unlock(&s_vfs_table_lock);
}

void eos_vfs_init(void) {
  if (s_vfs_initialized) return;

  bool prev_h = eos_proc_heap_tracking_disable();

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&s_vfs_table_lock, &attr);

  kv_init(s_fd_descs);
  kv_init(s_dir_descs);

  // Pre-seed standard file descriptors 0, 1, 2 in PSRAM
  for (int i = 0; i < 3; i++) {
    eos_vfs_fd_desc_t *desc = eos_psram_calloc_prefer(1, sizeof(eos_vfs_fd_desc_t));
    if (desc) {
      pthread_mutex_init(&desc->lock, &attr);
      atomic_init(&desc->ref_count, 1);
      desc->flags = 0;
      desc->in_use = true;
      kv_push(eos_vfs_fd_desc_t *, s_fd_descs, desc);
    }
  }

  pthread_mutexattr_destroy(&attr);
  s_vfs_initialized = true;

  eos_proc_heap_tracking_restore(prev_h);
}

void eos_vfs_fd_register(int fd, int flags) {
  if (fd < 0) return;
  pthread_mutex_lock(&s_vfs_table_lock);
  bool prev_h = eos_proc_heap_tracking_disable();

  if ((size_t)fd >= kv_size(s_fd_descs)) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    while (kv_size(s_fd_descs) <= (size_t)fd) {
      eos_vfs_fd_desc_t *desc = eos_psram_calloc_prefer(1, sizeof(eos_vfs_fd_desc_t));
      if (desc) {
        pthread_mutex_init(&desc->lock, &attr);
        atomic_init(&desc->ref_count, 0);
        desc->flags = 0;
        desc->in_use = false;
        kv_push(eos_vfs_fd_desc_t *, s_fd_descs, desc);
      }
    }
    pthread_mutexattr_destroy(&attr);
  }

  eos_vfs_fd_desc_t *desc = ((size_t)fd < kv_size(s_fd_descs)) ? kv_A(s_fd_descs, fd) : NULL;
  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&s_vfs_table_lock);

  if (desc) {
    pthread_mutex_lock(&desc->lock);
    atomic_store(&desc->ref_count, 1);
    desc->flags = flags;
    desc->in_use = true;
    pthread_mutex_unlock(&desc->lock);
  }
}

int eos_vfs_fd_ref(int fd) {
  if (fd < 0) return 1;
  eos_vfs_fd_desc_t *desc = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs)) {
    desc = kv_A(s_fd_descs, fd);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (desc) {
    return atomic_fetch_add(&desc->ref_count, 1) + 1;
  }
  return 1;
}

int eos_vfs_fd_unref(int fd) {
  if (fd < 0) return 0;
  eos_vfs_fd_desc_t *desc = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs)) {
    desc = kv_A(s_fd_descs, fd);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (desc) {
    return atomic_fetch_sub(&desc->ref_count, 1) - 1;
  }
  return 0;
}

void eos_vfs_fd_lock(int fd) {
  if (fd < 0) return;
  eos_vfs_fd_desc_t *desc = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs)) {
    desc = kv_A(s_fd_descs, fd);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (desc) {
    pthread_mutex_lock(&desc->lock);
  }
}

void eos_vfs_fd_unlock(int fd) {
  if (fd < 0) return;
  eos_vfs_fd_desc_t *desc = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs)) {
    desc = kv_A(s_fd_descs, fd);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (desc) {
    pthread_mutex_unlock(&desc->lock);
  }
}

void eos_vfs_fd_unregister(int fd) {
  if (fd < 0) return;
  eos_vfs_fd_desc_t *desc = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs)) {
    desc = kv_A(s_fd_descs, fd);
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (desc) {
    desc->in_use = false;
    desc->flags = 0;
    atomic_store(&desc->ref_count, 0);
  }
}

bool eos_vfs_fd_is_valid(int fd) {
  if (fd < 0) return false;
  bool valid = false;
  pthread_mutex_lock(&s_vfs_table_lock);
  if ((size_t)fd < kv_size(s_fd_descs) && kv_A(s_fd_descs, fd)) {
    valid = kv_A(s_fd_descs, fd)->in_use;
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  return valid;
}

void eos_vfs_dir_register(DIR *pdir) {
  if (!pdir) return;
  pthread_mutex_lock(&s_vfs_table_lock);
  bool prev_h = eos_proc_heap_tracking_disable();
  bool placed = false;

  for (size_t i = 0; i < kv_size(s_dir_descs); i++) {
    eos_vfs_dir_desc_t *d = kv_A(s_dir_descs, i);
    if (d && !d->in_use) {
      d->pdir = pdir;
      atomic_store(&d->ref_count, 1);
      d->in_use = true;
      placed = true;
      break;
    }
  }

  if (!placed) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    eos_vfs_dir_desc_t *d = eos_psram_calloc_prefer(1, sizeof(eos_vfs_dir_desc_t));
    if (d) {
      pthread_mutex_init(&d->lock, &attr);
      atomic_init(&d->ref_count, 1);
      d->pdir = pdir;
      d->in_use = true;
      kv_push(eos_vfs_dir_desc_t *, s_dir_descs, d);
    }
    pthread_mutexattr_destroy(&attr);
  }

  eos_proc_heap_tracking_restore(prev_h);
  pthread_mutex_unlock(&s_vfs_table_lock);
}

void eos_vfs_dir_lock(DIR *pdir) {
  if (!pdir) return;
  eos_vfs_dir_desc_t *found = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  for (size_t i = 0; i < kv_size(s_dir_descs); i++) {
    eos_vfs_dir_desc_t *d = kv_A(s_dir_descs, i);
    if (d && d->in_use && d->pdir == pdir) {
      found = d;
      break;
    }
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (found) {
    pthread_mutex_lock(&found->lock);
  }
}

void eos_vfs_dir_unlock(DIR *pdir) {
  if (!pdir) return;
  eos_vfs_dir_desc_t *found = NULL;
  pthread_mutex_lock(&s_vfs_table_lock);
  for (size_t i = 0; i < kv_size(s_dir_descs); i++) {
    eos_vfs_dir_desc_t *d = kv_A(s_dir_descs, i);
    if (d && d->in_use && d->pdir == pdir) {
      found = d;
      break;
    }
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
  if (found) {
    pthread_mutex_unlock(&found->lock);
  }
}

void eos_vfs_dir_close(DIR *pdir) {
  if (!pdir) return;
  pthread_mutex_lock(&s_vfs_table_lock);
  for (size_t i = 0; i < kv_size(s_dir_descs); i++) {
    eos_vfs_dir_desc_t *d = kv_A(s_dir_descs, i);
    if (d && d->in_use && d->pdir == pdir) {
      pthread_mutex_lock(&d->lock);
      d->in_use = false;
      d->pdir = NULL;
      atomic_store(&d->ref_count, 0);
      pthread_mutex_unlock(&d->lock);
      break;
    }
  }
  pthread_mutex_unlock(&s_vfs_table_lock);
}
