#include "ecore/fs.h"
#include "ecore/vfs.h"
#include "ecore/proc.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/log.h"
#include "ecore/ioctl.h"
#include "ecore/diskpart.h"
#include "emisc/kvec.h"
#include "emisc/fancymacro.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static kvec_psram_t(eos_mount_entry_t *) s_mount_table;
static pthread_mutex_t s_fs_lock = PTHREAD_MUTEX_INITIALIZER;
static bool s_fs_initialized = false;
static eos_post_mount_hook_t s_post_mount_hook = NULL;
static eos_pre_umount_hook_t s_pre_umount_hook = NULL;

// ── Normalize Path Helper ─────────────────────────────────────────
static void eos_fs_normalize_path(const char *in, char *out, size_t out_sz) {
  if (!in || !out || out_sz == 0) return;
  strlcpy(out, in, out_sz);
  size_t len = strlen(out);
  while (len > 1 && out[len - 1] == '/') {
    out[len - 1] = '\0';
    len--;
  }
}

// ── Internal Mount Table Record Add ──────────────────────────────
static void eos_fs_add_mount_record(const char *source, const char *target,
                                    const char *fstype, uint32_t flags,
                                    const char *opts, void *ctx,
                                    const eos_fs_type_t *fs) {
  bool prev_h = eos_proc_heap_tracking_disable();
  eos_mount_entry_t *entry = eos_psram_calloc_prefer(1, sizeof(eos_mount_entry_t));
  if (!entry) {
    eos_proc_heap_tracking_restore(prev_h);
    return;
  }
  strlcpy(entry->source, source ? source : "none", sizeof(entry->source));
  strlcpy(entry->target, target ? target : "/", sizeof(entry->target));
  strlcpy(entry->fstype, fstype ? fstype : "unknown", sizeof(entry->fstype));
  strlcpy(entry->opts, opts ? opts : ((flags & EOS_MOUNT_FLAG_RDONLY) ? "ro" : "rw"),
          sizeof(entry->opts));
  entry->flags = flags;
  entry->ctx = ctx;
  entry->fs = fs;
  entry->in_use = true;

  kv_push(eos_mount_entry_t *, s_mount_table, entry);
  eos_proc_heap_tracking_restore(prev_h);
}

// ── Subsystem Init ────────────────────────────────────────────────
void eos_fs_init(void) {
  pthread_mutex_lock(&s_fs_lock);
  if (s_fs_initialized) {
    pthread_mutex_unlock(&s_fs_lock);
    return;
  }
  bool prev_h = eos_proc_heap_tracking_disable();
  kv_init(s_mount_table);
  kv_resize(eos_mount_entry_t *, s_mount_table, EOS_FS_DEFAULT_MOUNT_CAPACITY);
  eos_proc_heap_tracking_restore(prev_h);

  s_fs_initialized = true;

  // Pre-seed core in-memory filesystem mounts using registered types
  eos_fs_add_mount_record("none", "/", "tmpfs", EOS_MOUNT_FLAG_NONE, "rw", NULL, eos_fs_find("tmpfs"));
  eos_fs_add_mount_record("none", "/dev", "devfs", EOS_MOUNT_FLAG_NONE, "rw", NULL, eos_fs_find("devfs"));
  eos_fs_add_mount_record("none", "/bin", "binfs", EOS_MOUNT_FLAG_RDONLY, "ro", NULL, eos_fs_find("binfs"));
  eos_fs_add_mount_record("none", "/proc", "procfs", EOS_MOUNT_FLAG_NONE, "rw", NULL, eos_fs_find("procfs"));
  eos_fs_add_mount_record("none", "/proc/pipes", "pipefs", EOS_MOUNT_FLAG_NONE, "rw", NULL, eos_fs_find("pipefs"));

  pthread_mutex_unlock(&s_fs_lock);
}

// ── Registry Lookup ───────────────────────────────────────────────
const eos_fs_type_t *eos_fs_find(const char *name) {
  if (!name || name[0] == '\0') return NULL;
  for (const eos_fs_type_t *fs = _eos_fs_start; fs < _eos_fs_end; ++fs) {
    if (fs->name && strcmp(fs->name, name) == 0) {
      return fs;
    }
  }
  return NULL;
}

// ── Busy & Device Query ───────────────────────────────────────────
bool eos_mount_is_busy(const char *target) {
  char norm[EOS_LINE_64_LEN];
  eos_fs_normalize_path(target, norm, sizeof(norm));
  return eos_vfs_is_mount_busy(norm);
}

bool eos_mount_is_device_mounted(const char *device_path) {
  if (!device_path) return false;
  if (!s_fs_initialized) eos_fs_init();

  const char *dev_name = device_path;
  if (strncmp(dev_name, "/dev/", 5) == 0) dev_name += 5;

  pthread_mutex_lock(&s_fs_lock);
  for (size_t i = 0; i < kv_size(s_mount_table); i++) {
    eos_mount_entry_t *m = kv_A(s_mount_table, i);
    if (!m || !m->in_use) continue;
    const char *src = m->source;
    if (strncmp(src, "/dev/", 5) == 0) src += 5;
    if (strcmp(src, dev_name) == 0) {
      pthread_mutex_unlock(&s_fs_lock);
      return true;
    }
  }
  pthread_mutex_unlock(&s_fs_lock);
  return false;
}

// ── Auto Detect Type ──────────────────────────────────────────────
const char *eos_fs_detect_type(const char *bdev_path) {
  if (!bdev_path) return NULL;
  const char *dev_name = bdev_path;
  if (strncmp(dev_name, "/dev/", 5) == 0) dev_name += 5;
  eos_dev_t *dev = eos_dev_find_by_name(dev_name);
  if (!dev) return "fat";

  uint8_t part_type = 0;
  if (eos_dev_ioctl_call(dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_GET_PART_TYPE, &part_type) == 0) {
    if (eos_diskpart_is_fat(part_type)) {
      return "vfat";
    }
  }
  return "fat";
}

// ── Mount Operation ───────────────────────────────────────────────
int eos_mount(const char *source, const char *target, const char *fstype,
              uint32_t flags, const char *opts) {
  if (!target || target[0] != '/') {
    errno = EINVAL;
    return -1;
  }
  if (!s_fs_initialized) eos_fs_init();

  char norm_target[EOS_LINE_64_LEN];
  eos_fs_normalize_path(target, norm_target, sizeof(norm_target));

  pthread_mutex_lock(&s_fs_lock);
  for (size_t i = 0; i < kv_size(s_mount_table); i++) {
    eos_mount_entry_t *m = kv_A(s_mount_table, i);
    if (!m || !m->in_use) continue;
    if (strcmp(m->target, norm_target) == 0) {
      pthread_mutex_unlock(&s_fs_lock);
      errno = EBUSY;
      return -1;
    }
    if (source && strncmp(source, "/dev/", 5) == 0 && strcmp(m->source, source) == 0) {
      pthread_mutex_unlock(&s_fs_lock);
      errno = EBUSY;
      return -1;
    }
  }
  pthread_mutex_unlock(&s_fs_lock);

  const char *resolved_type = fstype;
  if (!resolved_type || resolved_type[0] == '\0' || strcmp(resolved_type, "auto") == 0) {
    if (source && strncmp(source, "/dev/", 5) == 0) {
      resolved_type = eos_fs_detect_type(source);
    } else {
      resolved_type = "tmpfs";
    }
  }

  const eos_fs_type_t *fs = eos_fs_find(resolved_type);
  if (!fs) {
    errno = ENODEV;
    return -1;
  }

  if ((fs->flags & EOS_FS_FLAG_NEEDS_BDEV) && (!source || strncmp(source, "/dev/", 5) != 0)) {
    errno = EINVAL;
    return -1;
  }

  void *ctx = NULL;
  if (fs->mount) {
    int rc = fs->mount(source, norm_target, flags, opts, &ctx);
    if (rc != 0) {
      errno = EIO;
      return -1;
    }
  }

  pthread_mutex_lock(&s_fs_lock);
  eos_fs_add_mount_record(source, norm_target, fs->name, flags, opts, ctx, fs);
  eos_post_mount_hook_t hook = s_post_mount_hook;
  pthread_mutex_unlock(&s_fs_lock);

  if (hook) {
    hook(source ? source : "none", norm_target, fs->name);
  }
  return 0;
}

// ── Umount Operation ──────────────────────────────────────────────
int eos_umount(const char *target_or_device, uint32_t flags) {
  if (!target_or_device || target_or_device[0] == '\0') {
    errno = EINVAL;
    return -1;
  }
  if (!s_fs_initialized) eos_fs_init();

  char norm[EOS_LINE_64_LEN];
  eos_fs_normalize_path(target_or_device, norm, sizeof(norm));

  pthread_mutex_lock(&s_fs_lock);
  eos_mount_entry_t *matched = NULL;
  size_t matched_idx = 0;

  for (size_t i = 0; i < kv_size(s_mount_table); i++) {
    eos_mount_entry_t *m = kv_A(s_mount_table, i);
    if (!m || !m->in_use) continue;
    if (strcmp(m->target, norm) == 0 || strcmp(m->source, norm) == 0) {
      matched = m;
      matched_idx = i;
      break;
    }
  }

  if (!matched) {
    pthread_mutex_unlock(&s_fs_lock);
    errno = ENOENT;
    return -1;
  }

  if (strcmp(matched->target, "/") == 0) {
    pthread_mutex_unlock(&s_fs_lock);
    errno = EBUSY;
    return -1;
  }

  if (eos_vfs_is_mount_busy(matched->target)) {
    pthread_mutex_unlock(&s_fs_lock);
    errno = EBUSY;
    return -1;
  }

  eos_pre_umount_hook_t hook = s_pre_umount_hook;
  pthread_mutex_unlock(&s_fs_lock);

  if (hook) {
    hook(matched->target);
  }

  if (matched->fs && matched->fs->umount) {
    int rc = matched->fs->umount(matched->target, matched->ctx, flags);
    if (rc != 0) {
      errno = EIO;
      return -1;
    }
  }

  pthread_mutex_lock(&s_fs_lock);
  matched->in_use = false;
  kv_A(s_mount_table, matched_idx) = NULL;
  eos_psram_free(matched);
  pthread_mutex_unlock(&s_fs_lock);

  return 0;
}

// ── Hook Registration ─────────────────────────────────────────────
int eos_mount_register_hooks(eos_post_mount_hook_t post_mount, eos_pre_umount_hook_t pre_umount) {
  pthread_mutex_lock(&s_fs_lock);
  s_post_mount_hook = post_mount;
  s_pre_umount_hook = pre_umount;
  pthread_mutex_unlock(&s_fs_lock);
  return 0;
}

// ── Procfs Diagnostics ────────────────────────────────────────────
char *eos_fs_generate_filesystems_proc(void) {
  size_t cap = EOS_FS_PROC_FILESYSTEMS_BUF_SIZE;
  char *buf = eos_psram_calloc_prefer(1, cap);
  if (!buf) buf = calloc(1, cap);
  if (!buf) return NULL;

  size_t len = 0;
  for (const eos_fs_type_t *fs = _eos_fs_start; fs < _eos_fs_end; ++fs) {
    if (!fs->name) continue;
    int written;
    if (fs->flags & EOS_FS_FLAG_PSEUDO) {
      written = snprintf(buf + len, cap - len, "nodev\t%s\n", fs->name);
    } else {
      written = snprintf(buf + len, cap - len, "\t%s\n", fs->name);
    }
    if (written > 0 && (size_t)written < cap - len) {
      len += written;
    }
  }
  return buf;
}

char *eos_fs_generate_mounts_proc(void) {
  if (!s_fs_initialized) eos_fs_init();
  size_t cap = EOS_FS_PROC_MOUNTS_BUF_SIZE;
  char *buf = eos_psram_calloc_prefer(1, cap);
  if (!buf) buf = calloc(1, cap);
  if (!buf) return NULL;

  size_t len = 0;
  pthread_mutex_lock(&s_fs_lock);
  for (size_t i = 0; i < kv_size(s_mount_table); i++) {
    eos_mount_entry_t *m = kv_A(s_mount_table, i);
    if (!m || !m->in_use) continue;
    int written = snprintf(buf + len, cap - len, "%s %s %s %s 0 0\n",
                           m->source[0] ? m->source : "none",
                           m->target,
                           m->fstype,
                           m->opts[0] ? m->opts : "rw");
    if (written > 0 && (size_t)written < cap - len) {
      len += written;
    }
  }
  pthread_mutex_unlock(&s_fs_lock);
  return buf;
}

size_t eos_fs_get_mount_count(void) {
  if (!s_fs_initialized) eos_fs_init();
  pthread_mutex_lock(&s_fs_lock);
  size_t count = 0;
  for (size_t i = 0; i < kv_size(s_mount_table); i++) {
    eos_mount_entry_t *m = kv_A(s_mount_table, i);
    if (m && m->in_use) count++;
  }
  pthread_mutex_unlock(&s_fs_lock);
  return count;
}

int eos_fs_get_mount_entry(size_t index, eos_mount_entry_t *out_entry) {
  if (!out_entry) return -1;
  if (!s_fs_initialized) eos_fs_init();
  pthread_mutex_lock(&s_fs_lock);
  size_t current = 0;
  for (size_t i = 0; i < kv_size(s_mount_table); i++) {
    eos_mount_entry_t *m = kv_A(s_mount_table, i);
    if (m && m->in_use) {
      if (current == index) {
        *out_entry = *m;
        pthread_mutex_unlock(&s_fs_lock);
        return 0;
      }
      current++;
    }
  }
  pthread_mutex_unlock(&s_fs_lock);
  return -1;
}
