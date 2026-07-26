#include "binfs.h"
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
  const eos_native_app_manifest_t *app; // Pointer to app manifest
  off_t offset;                         // Current read position in manifest
  bool in_use;                          // Whether this FD is open
} binfs_fd_t;

// ── Static FD Table ────────────────────────────────────────────

static binfs_fd_t binfs_fds[EOS_BINFS_MAX_FDS] = {0};

// Get number of registered native apps
static inline uint32_t binfs_app_count(void) {
  return (uint32_t)(_eos_apps_end - _eos_apps_start);
}

// Find app by name (filename field)
static const eos_native_app_manifest_t *binfs_find_app(const char *name) {
  if (!name)
    return NULL;

  for (const eos_native_app_manifest_t *app = _eos_apps_start;
       app < _eos_apps_end; app++) {
    if (strcmp(app->filename, name) == 0)
      return app;
  }
  return NULL;
}

// Find app by index
static const eos_native_app_manifest_t *binfs_get_app(uint32_t idx) {
  if (idx >= binfs_app_count())
    return NULL;
  return &_eos_apps_start[idx];
}

// ── FD Management ──────────────────────────────────────────────

static int binfs_fd_alloc(const eos_native_app_manifest_t *app) {
  for (int i = 0; i < EOS_BINFS_MAX_FDS; i++) {
    if (!binfs_fds[i].in_use) {
      binfs_fds[i].app = app;
      binfs_fds[i].offset = 0;
      binfs_fds[i].in_use = true;
      return i;
    }
  }
  return -1; // No free FDs
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
    binfs_fds[fd].app = NULL;
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

  // Strip leading slash
  const char *name = path;
  if (*name == '/')
    name++;

  // Find the app
  const eos_native_app_manifest_t *app = binfs_find_app(name);
  if (!app) {
    errno = ENOENT;
    return -1;
  }

  // Allocate FD
  int fd = binfs_fd_alloc(app);
  if (fd < 0) {
    errno = ENFILE;
    return -1;
  }

  return fd;
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

  const eos_native_app_manifest_t *app = binfs_fd->app;
  if (!app)
    return 0;

  // Calculate how much we can read from the manifest
  off_t manifest_size = sizeof(eos_native_app_manifest_t);
  off_t remaining = manifest_size - binfs_fd->offset;

  if (remaining <= 0)
    return 0; // EOF

  // Clamp read to available data
  size_t to_read = (len < remaining) ? len : remaining;

  // Copy from manifest
  uint8_t *src = (uint8_t *)app + binfs_fd->offset;
  memcpy(buf, src, to_read);

  binfs_fd->offset += to_read;
  return to_read;
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

  off_t manifest_size = sizeof(eos_native_app_manifest_t);
  off_t new_offset = 0;

  switch (whence) {
  case SEEK_SET:
    new_offset = offset;
    break;
  case SEEK_CUR:
    new_offset = binfs_fd->offset + offset;
    break;
  case SEEK_END:
    new_offset = manifest_size + offset;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  // Clamp to valid range
  if (new_offset < 0)
    new_offset = 0;
  if (new_offset > manifest_size)
    new_offset = manifest_size;

  binfs_fd->offset = new_offset;
  return new_offset;
}

// ── Directory Operations ───────────────────────────────────────

typedef struct {
  uint32_t esp_idf_fs_index;
  uint32_t app_idx;
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

  dir->app_idx = 0;
  return (DIR *)dir;
}

static struct dirent *binfs_readdir(void *ctx, DIR *pdir) {
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  if (!dir) {
    errno = EBADF;
    return NULL;
  }

  const eos_native_app_manifest_t *app = binfs_get_app(dir->app_idx);
  if (!app)
    return NULL; // End of directory

  dir->entry.d_ino = dir->app_idx;
  dir->entry.d_type = DT_REG;
  strlcpy(dir->entry.d_name, app->filename, sizeof(dir->entry.d_name));

  dir->app_idx++;
  return &dir->entry;
}

static long binfs_telldir(void *ctx, DIR *pdir) {
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  if (!dir)
    return -1;
  return (long)dir->app_idx;
}

static void binfs_seekdir(void *ctx, DIR *pdir, long offset) {
  binfs_dir_t *dir = (binfs_dir_t *)pdir;
  if (!dir)
    return;
  dir->app_idx = (uint32_t)offset;
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
    binfs_fds[i].app = NULL;
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
      .opendir_p = binfs_opendir,
      .readdir_p = binfs_readdir,
      .seekdir_p = binfs_seekdir,
      .telldir_p = binfs_telldir,
      .closedir_p = binfs_closedir,
  };

  esp_vfs_register(EOS_BINFS_ROOT, &vfs, NULL);
  EOS_LOGI("binfs mounted at %s with %d registered apps", EOS_BINFS_ROOT,
           binfs_app_count());
}
