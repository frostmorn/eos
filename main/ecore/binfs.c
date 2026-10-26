#include "binfs.h"
#include "asset.h"
#include "emisc/fancymacro.h"
#include <dirent.h>
#include <errno.h>
#include <esp_vfs.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

// File descriptor entry for binfs

typedef struct {
  const eos_bin_t *bin;       // Pointer to bin manifest
  const eos_asset_t *asset;   // Pointer to asset
  off_t offset;               // Current read position
  bool in_use;                // Whether this FD is open
} binfs_fd_t;

// ── Static FD Table ────────────────────────────────────────────

static binfs_fd_t binfs_fds[EOS_BINFS_MAX_FDS] = {0};

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
  if (!name)
    return NULL;

  for (const eos_bin_t *bin = _eos_bins_start; bin < _eos_bins_end; bin++) {
    if (strcmp(bin->filename, name) == 0)
      return bin;
  }
  return NULL;
}

// Find bin by index
static const eos_bin_t *binfs_get_bin(uint32_t idx) {
  if (idx >= binfs_bin_count())
    return NULL;
  return &_eos_bins_start[idx];
}

// Find asset by name (filename field)
static const eos_asset_t *binfs_find_asset(const char *name) {
  if (!name)
    return NULL;

  const eos_asset_t *asset = eos_get_init_asset();
  if (strcmp(asset->filename, name) == 0)
    return asset;
  return NULL;
}

// Find asset by index
static const eos_asset_t *binfs_get_asset(uint32_t idx) {
  if (idx == 0)
    return eos_get_init_asset();
  return NULL;
}

// ── FD Management ──────────────────────────────────────────────

static int binfs_fd_alloc_bin(const eos_bin_t *bin) {
  for (int i = 0; i < EOS_BINFS_MAX_FDS; i++) {
    if (!binfs_fds[i].in_use) {
      binfs_fds[i].bin = bin;
      binfs_fds[i].asset = NULL;
      binfs_fds[i].offset = 0;
      binfs_fds[i].in_use = true;
      return i;
    }
  }
  return -1;
}

static int binfs_fd_alloc_asset(const eos_asset_t *asset) {
  for (int i = 0; i < EOS_BINFS_MAX_FDS; i++) {
    if (!binfs_fds[i].in_use) {
      binfs_fds[i].bin = NULL;
      binfs_fds[i].asset = asset;
      binfs_fds[i].offset = 0;
      binfs_fds[i].in_use = true;
      return i;
    }
  }
  return -1;
}

static binfs_fd_t *binfs_fd_get(int fd) {
  if (fd < 0 || fd >= EOS_BINFS_MAX_FDS)
    return NULL;
  if (!binfs_fds[fd].in_use)
    return NULL;
  return &binfs_fds[fd];
}

static void binfs_fd_free(int fd) {
  if (fd >= 0 && fd < EOS_BINFS_MAX_FDS) {
    binfs_fds[fd].in_use = false;
    binfs_fds[fd].bin = NULL;
    binfs_fds[fd].asset = NULL;
    binfs_fds[fd].offset = 0;
  }
}

// ── ESP-IDF VFS Callbacks ──────────────────────────────────────

static int binfs_open(void *ctx, const char *path, int flags, int mode) {
  // Reject write requests
  if (flags & (O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC)) {
    errno = EACCES;
    return -1;
  }

  if (!path) {
    errno = ENOENT;
    return -1;
  }

  // Strip leading slashes and optional bin/ or /bin/ prefix
  const char *name = path;
  while (*name == '/')
    name++;
  if (strncmp(name, "bin/", 4) == 0) {
    name += 4;
  }
  while (*name == '/')
    name++;

  // Find the bin
  const eos_bin_t *bin = binfs_find_bin(name);
  if (bin) {
    int fd = binfs_fd_alloc_bin(bin);
    if (fd < 0) {
      errno = ENFILE;
      return -1;
    }
    return fd;
  }

  // Find the asset
  const eos_asset_t *asset = binfs_find_asset(name);
  if (asset) {
    int fd = binfs_fd_alloc_asset(asset);
    if (fd < 0) {
      errno = ENFILE;
      return -1;
    }
    return fd;
  }

  errno = ENOENT;
  return -1;
}

static int binfs_close(void *ctx, int fd) {
  binfs_fd_t *binfs_fd = binfs_fd_get(fd);
  if (!binfs_fd) {
    errno = EBADF;
    return -1;
  }

  binfs_fd_free(fd);
  return 0;
}

static ssize_t binfs_read(void *ctx, int fd, void *buf, size_t len) {
  binfs_fd_t *binfs_fd = binfs_fd_get(fd);
  if (!binfs_fd) {
    errno = EBADF;
    return -1;
  }

  if (!buf || len == 0)
    return 0;

  if (binfs_fd->bin) {
    const eos_bin_t *bin = binfs_fd->bin;
    off_t manifest_size = sizeof(eos_bin_t);
    off_t remaining = manifest_size - binfs_fd->offset;
    if (remaining <= 0)
      return 0;
    size_t to_read = (len < (size_t)remaining) ? len : (size_t)remaining;
    uint8_t *src = (uint8_t *)bin + binfs_fd->offset;
    memcpy(buf, src, to_read);
    binfs_fd->offset += to_read;
    return (ssize_t)to_read;
  } else if (binfs_fd->asset) {
    const eos_asset_t *asset = binfs_fd->asset;
    off_t remaining = (off_t)asset->size - binfs_fd->offset;
    if (remaining <= 0)
      return 0;
    size_t to_read = (len < (size_t)remaining) ? len : (size_t)remaining;
    const uint8_t *src = asset->data + binfs_fd->offset;
    memcpy(buf, src, to_read);
    binfs_fd->offset += to_read;
    return (ssize_t)to_read;
  }

  return 0;
}

static ssize_t binfs_write(void *ctx, int fd, const void *buf, size_t len) {
  // Write is not allowed to binfs
  errno = EACCES;
  return -1;
}

static off_t binfs_lseek(void *ctx, int fd, off_t offset, int whence) {
  binfs_fd_t *binfs_fd = binfs_fd_get(fd);
  if (!binfs_fd) {
    errno = EBADF;
    return -1;
  }

  off_t max_size = 0;
  if (binfs_fd->bin) {
    max_size = sizeof(eos_bin_t);
  } else if (binfs_fd->asset) {
    max_size = (off_t)binfs_fd->asset->size;
  }

  off_t new_offset = 0;

  switch (whence) {
  case SEEK_SET:
    new_offset = offset;
    break;
  case SEEK_CUR:
    new_offset = binfs_fd->offset + offset;
    break;
  case SEEK_END:
    new_offset = max_size + offset;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  // Clamp to valid range
  if (new_offset < 0)
    new_offset = 0;
  if (new_offset > max_size)
    new_offset = max_size;

  binfs_fd->offset = new_offset;
  return new_offset;
}

static int binfs_fstat(void *ctx, int fd, struct stat *st) {
  binfs_fd_t *binfs_fd = binfs_fd_get(fd);
  if (!binfs_fd || !st) {
    errno = EBADF;
    return -1;
  }

  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFREG | 0555;
  if (binfs_fd->bin) {
    st->st_size = sizeof(eos_bin_t);
  } else if (binfs_fd->asset) {
    st->st_size = (off_t)binfs_fd->asset->size;
  }
  st->st_blksize = 512;
  return 0;
}

static int binfs_stat(void *ctx, const char *path, struct stat *st) {
  if (!path || !st) {
    errno = EINVAL;
    return -1;
  }

  const char *name = path;
  while (*name == '/') name++;
  if (strncmp(name, "bin/", 4) == 0) name += 4;
  while (*name == '/') name++;

  if (*name == '\0') {
    // Root /bin directory
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

typedef struct {
  uint32_t esp_idf_fs_index;
  uint32_t bin_idx;
  struct dirent entry;
} binfs_dir_t;

static DIR *binfs_opendir(void *ctx, const char *path) {
  // Only allow opening root directory
  if (!path || (strcmp(path, "/") != 0 && strcmp(path, "") != 0)) {
    errno = ENOTDIR;
    return NULL;
  }

  binfs_dir_t *dir = malloc(sizeof(binfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }

  dir->bin_idx = 0;
  return (DIR *)dir;
}

static struct dirent *binfs_readdir(void *ctx, DIR *pdir) {
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

  return NULL; // End of directory
}

static long binfs_telldir(void *ctx, DIR *pdir) {
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  if (!dir)
    return -1;
  return (long)dir->bin_idx;
}

static void binfs_seekdir(void *ctx, DIR *pdir, long offset) {
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  if (!dir)
    return;
  dir->bin_idx = (uint32_t)offset;
}

static int binfs_closedir(void *ctx, DIR *pdir) {
  if (pdir)
    free(pdir);
  return 0;
}

// ── Initialization ────────────────────────────────────────────

void eos_binfs_init(void) {
  // Initialize FD table
  for (int i = 0; i < EOS_BINFS_MAX_FDS; i++) {
    binfs_fds[i].in_use = false;
    binfs_fds[i].bin = NULL;
    binfs_fds[i].asset = NULL;
    binfs_fds[i].offset = 0;
  }

  // Register VFS
  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,
      .open_p = binfs_open,
      .close_p = binfs_close,
      .read_p = binfs_read,
      .write_p = binfs_write,
      .lseek_p = binfs_lseek,
      .fstat_p = binfs_fstat,
      .stat_p = binfs_stat,
      .opendir_p = binfs_opendir,
      .readdir_p = binfs_readdir,
      .seekdir_p = binfs_seekdir,
      .telldir_p = binfs_telldir,
      .closedir_p = binfs_closedir,
  };
  mkdir(EOS_BINFS_ROOT, 0755);
  esp_err_t err = esp_vfs_register(EOS_BINFS_ROOT, &vfs, NULL);
  if (err != ESP_OK) {
    EOS_LOGE("Failed to register binfs at %s: %s", EOS_BINFS_ROOT, esp_err_to_name(err));
  } else {
    EOS_LOGI("binfs mounted at %s with %d registered bins and %d registered assets", EOS_BINFS_ROOT,
             binfs_bin_count(), binfs_asset_count());
  }
}
