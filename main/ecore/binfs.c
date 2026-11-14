#include "binfs.h"
#include "asset.h"
#include "ecore/vfs.h"
#include "emisc/fancymacro.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// Enlarged VFS file structure for binfs
typedef struct {
  eos_vfs_file_t base;
  const eos_bin_t *bin;       // Pointer to bin manifest
  const eos_asset_t *asset;   // Pointer to asset
  off_t offset;               // Current read position
} binfs_file_t;

// Enlarged VFS directory structure for binfs
typedef struct {
  eos_vfs_dir_t base;
  uint32_t bin_idx;
  struct dirent entry;
} binfs_dir_t;

static pthread_mutex_t s_binfs_lock = PTHREAD_MUTEX_INITIALIZER;

// Get number of registered native bins
static inline uint32_t binfs_bin_count(void) {
  return (uint32_t)(_eos_bins_end - _eos_bins_start);
}

// Get number of registered assets
static inline uint32_t binfs_asset_count(void) {
  return 1;
}

// Find bin by name (filename field)
static const eos_bin_t *binfs_find_bin(const char *name) {
  if (!name) return NULL;
  for (const eos_bin_t *bin = _eos_bins_start; bin < _eos_bins_end; bin++) {
    if (strcmp(bin->filename, name) == 0) return bin;
  }
  return NULL;
}

// Find bin by index
static const eos_bin_t *binfs_get_bin(uint32_t idx) {
  if (idx >= binfs_bin_count()) return NULL;
  return &_eos_bins_start[idx];
}

// Find asset by name (filename field)
static const eos_asset_t *binfs_find_asset(const char *name) {
  if (!name) return NULL;
  const eos_asset_t *asset = eos_get_init_asset();
  if (strcmp(asset->filename, name) == 0) return asset;
  return NULL;
}

// Find asset by index
static const eos_asset_t *binfs_get_asset(uint32_t idx) {
  if (idx == 0) return eos_get_init_asset();
  return NULL;
}

// ── VFS Operations ─────────────────────────────────────────────

static int binfs_open(void *ctx, const char *path, int flags, int mode, eos_vfs_file_t **out_file) {
  (void)ctx;
  (void)mode;

  if (flags & (O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC)) {
    errno = EACCES;
    return -1;
  }

  if (!path) {
    errno = ENOENT;
    return -1;
  }

  const char *name = path;
  while (*name == '/') name++;
  if (strncmp(name, "bin/", 4) == 0) name += 4;
  while (*name == '/') name++;

  pthread_mutex_lock(&s_binfs_lock);
  const eos_bin_t *bin = binfs_find_bin(name);
  const eos_asset_t *asset = bin ? NULL : binfs_find_asset(name);
  pthread_mutex_unlock(&s_binfs_lock);

  if (!bin && !asset) {
    errno = ENOENT;
    return -1;
  }

  binfs_file_t *file = eos_vfs_file_alloc(NULL, sizeof(binfs_file_t), flags);
  if (!file) {
    errno = ENFILE;
    return -1;
  }

  file->bin = bin;
  file->asset = asset;
  file->offset = 0;
  *out_file = &file->base;

  return file->base.fd;
}

static int binfs_close(void *ctx, eos_vfs_file_t *file) {
  (void)ctx;
  (void)file;
  return 0;
}

static ssize_t binfs_read(void *ctx, eos_vfs_file_t *file, void *buf, size_t len) {
  (void)ctx;
  binfs_file_t *bf = (binfs_file_t *)file;
  if (!buf || len == 0) return 0;

  if (bf->bin) {
    off_t manifest_size = sizeof(eos_bin_t);
    off_t remaining = manifest_size - bf->offset;
    if (remaining <= 0) return 0;
    size_t to_read = (len < (size_t)remaining) ? len : (size_t)remaining;
    const uint8_t *src = (const uint8_t *)bf->bin + bf->offset;
    memcpy(buf, src, to_read);
    bf->offset += to_read;
    return (ssize_t)to_read;
  } else if (bf->asset) {
    off_t remaining = (off_t)bf->asset->size - bf->offset;
    if (remaining <= 0) return 0;
    size_t to_read = (len < (size_t)remaining) ? len : (size_t)remaining;
    const uint8_t *src = bf->asset->data + bf->offset;
    memcpy(buf, src, to_read);
    bf->offset += to_read;
    return (ssize_t)to_read;
  }

  return 0;
}

static ssize_t binfs_write(void *ctx, eos_vfs_file_t *file, const void *buf, size_t len) {
  (void)ctx;
  (void)file;
  (void)buf;
  (void)len;
  errno = EACCES;
  return -1;
}

static off_t binfs_lseek(void *ctx, eos_vfs_file_t *file, off_t offset, int whence) {
  (void)ctx;
  binfs_file_t *bf = (binfs_file_t *)file;
  off_t max_size = bf->bin ? (off_t)sizeof(eos_bin_t) : (bf->asset ? (off_t)bf->asset->size : 0);
  off_t new_offset = 0;

  switch (whence) {
  case SEEK_SET:
    new_offset = offset;
    break;
  case SEEK_CUR:
    new_offset = bf->offset + offset;
    break;
  case SEEK_END:
    new_offset = max_size + offset;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  if (new_offset < 0) new_offset = 0;
  if (new_offset > max_size) new_offset = max_size;

  bf->offset = new_offset;
  return new_offset;
}

static int binfs_fstat(void *ctx, eos_vfs_file_t *file, struct stat *st) {
  (void)ctx;
  binfs_file_t *bf = (binfs_file_t *)file;
  if (!st) {
    errno = EINVAL;
    return -1;
  }

  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFREG | 0555;
  if (bf->bin) {
    st->st_size = sizeof(eos_bin_t);
  } else if (bf->asset) {
    st->st_size = (off_t)bf->asset->size;
  }
  st->st_blksize = 512;
  return 0;
}

static int binfs_stat(void *ctx, const char *path, struct stat *st) {
  (void)ctx;
  if (!path || !st) {
    errno = EINVAL;
    return -1;
  }

  const char *name = path;
  while (*name == '/') name++;
  if (strncmp(name, "bin/", 4) == 0) name += 4;
  while (*name == '/') name++;

  if (*name == '\0') {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    return 0;
  }

  const eos_bin_t *bin = binfs_find_bin(name);
  if (bin) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0555;
    st->st_size = sizeof(eos_bin_t);
    st->st_blksize = 512;
    return 0;
  }

  const eos_asset_t *asset = binfs_find_asset(name);
  if (asset) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0555;
    st->st_size = (off_t)asset->size;
    st->st_blksize = 512;
    return 0;
  }

  errno = ENOENT;
  return -1;
}

// ── Directory Operations ───────────────────────────────────────

static int binfs_opendir(void *ctx, const char *path, eos_vfs_dir_t **out_dir) {
  (void)ctx;
  if (!path || (strcmp(path, "/") != 0 && strcmp(path, "") != 0)) {
    errno = ENOTDIR;
    return -1;
  }

  binfs_dir_t *dir = eos_vfs_dir_alloc(NULL, sizeof(binfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return -1;
  }

  dir->bin_idx = 0;
  *out_dir = &dir->base;
  return 0;
}

static struct dirent *binfs_readdir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  if (!dir) {
    errno = EBADF;
    return NULL;
  }

  uint32_t bin_count = binfs_bin_count();
  if (dir->bin_idx < bin_count) {
    const eos_bin_t *bin = binfs_get_bin(dir->bin_idx);
    if (bin) {
      dir->entry.d_ino = dir->bin_idx;
      dir->entry.d_type = DT_REG;
      strlcpy(dir->entry.d_name, bin->filename, sizeof(dir->entry.d_name));
      dir->bin_idx++;
      return &dir->entry;
    }
  }

  uint32_t asset_idx = dir->bin_idx - bin_count;
  const eos_asset_t *asset = binfs_get_asset(asset_idx);
  if (asset) {
    dir->entry.d_ino = dir->bin_idx;
    dir->entry.d_type = DT_REG;
    strlcpy(dir->entry.d_name, asset->filename, sizeof(dir->entry.d_name));
    dir->bin_idx++;
    return &dir->entry;
  }

  return NULL;
}

static int binfs_closedir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  (void)pdir;
  return 0;
}

static void binfs_seekdir(void *ctx, eos_vfs_dir_t *pdir, long loc) {
  (void)ctx;
  if (!pdir) return;
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  dir->bin_idx = (uint32_t)loc;
}

static long binfs_telldir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  if (!pdir) return -1;
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  return (long)dir->bin_idx;
}

// ── Initialization ────────────────────────────────────────────

void eos_binfs_init(void) {
  static const eos_vfs_ops_t ops = {
      .open = binfs_open,
      .close = binfs_close,
      .read = binfs_read,
      .write = binfs_write,
      .lseek = binfs_lseek,
      .fstat = binfs_fstat,
      .stat = binfs_stat,
      .opendir = binfs_opendir,
      .readdir = binfs_readdir,
      .closedir = binfs_closedir,
      .seekdir = binfs_seekdir,
      .telldir = binfs_telldir,
  };

  mkdir(EOS_BINFS_ROOT, 0755);
  esp_err_t err = eos_vfs_register_ops(EOS_BINFS_ROOT, &ops, NULL);
  if (err != ESP_OK) {
    EOS_LOGE("Failed to register binfs at %s: %s", EOS_BINFS_ROOT, esp_err_to_name(err));
  } else {
    EOS_LOGI("binfs mounted at %s with %d registered bins and %d registered assets", EOS_BINFS_ROOT,
             binfs_bin_count(), binfs_asset_count());
  }
}
