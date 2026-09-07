#include "devfs.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "emisc/fancymacro.h"
#include "emisc/kvec.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////
// FD -> DEV
//
// fd  = devfs/external fd
// drvfd = driver's fd
//
// The vector index is NOT an fd. kv_drop_fast() is therefore safe.
///////////////////////////////////////////////////////////////////

typedef struct {
  int fd;
  int drvfd;
  eos_dev_t *dev;
} devfs_fdmap_t;

///////////////////////////////////////////////////////////////////
// DIR -> DEV
///////////////////////////////////////////////////////////////////

typedef struct {
  /*
   * Must be first. ESP-IDF VFS uses this field internally.
   */
  uint32_t esp_idf_fs_index;

  bool root_listing;
  uint32_t index;

  eos_dev_t *dev;
  DIR *drvdir;

  struct dirent entry;
} devfs_dir_t;

///////////////////////////////////////////////////////////////////
// DEVFS state
///////////////////////////////////////////////////////////////////

typedef struct {
  kvec_t(devfs_fdmap_t) fds;
} devfs_state_t;

static devfs_state_t *eos_devfs_state = NULL;

///////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////

static eos_dev_t *devfs_resolve(const char *path) {
  if (!path)
    return NULL;

  /*
   * VFS callbacks normally receive a path relative to the mount.
   * Therefore both "" and "/" represent the devfs root.
   */
  if (*path == '/')
    path++;

  if (*path == '\0')
    return NULL;

  return eos_dev_find_by_name(path);
}

static bool devfs_is_root(const char *path) {
  if (!path || *path == '\0')
    return true;

  return path[0] == '/' && path[1] == '\0';
}

static devfs_fdmap_t *devfs_fd_to_fdmap(devfs_state_t *dstate, int fd) {
  for (size_t i = 0; i < kv_size(dstate->fds); ++i) {
    devfs_fdmap_t *fdmap = &kv_A(dstate->fds, i);

    if (fdmap->fd == fd)
      return fdmap;
  }

  errno = EBADF;
  return NULL;
}

/*
 * Find the lowest unused devfs fd.
 *
 * For the expected small number of /dev descriptors this keeps the
 * implementation simple while naturally reusing closed descriptors.
 */
static int devfs_alloc_fd(devfs_state_t *dstate, eos_dev_t *dev, int drvfd) {
  int fd = 0;

  for (;;) {
    bool used = false;

    for (size_t i = 0; i < kv_size(dstate->fds); ++i) {
      if (kv_A(dstate->fds, i).fd == fd) {
        used = true;
        break;
      }
    }

    if (!used)
      break;

    fd++;
  }

  devfs_fdmap_t fdmap = {
      .fd = fd,
      .drvfd = drvfd,
      .dev = dev,
  };

  kv_push(devfs_fdmap_t, dstate->fds, fdmap);

  return fd;
}

///////////////////////////////////////////////////////////////////
// FD operations
///////////////////////////////////////////////////////////////////

ssize_t eos_devfs_write(devfs_state_t *dstate, int fd, void *data,
                        size_t size) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_write(fdmap->dev, fdmap->drvfd, data, size);
}

off_t eos_devfs_lseek(devfs_state_t *dstate, int fd, off_t offset, int whence) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_lseek(fdmap->dev, fdmap->drvfd, offset, whence);
}

ssize_t eos_devfs_read(devfs_state_t *dstate, int fd, void *dst, size_t size) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_read(fdmap->dev, fdmap->drvfd, dst, size);
}

ssize_t eos_devfs_pread(devfs_state_t *dstate, int fd, void *dst, size_t size,
                        off_t offset) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_pread(fdmap->dev, fdmap->drvfd, dst, size, offset);
}

ssize_t eos_devfs_pwrite(devfs_state_t *dstate, int fd, void *src, size_t size,
                         off_t offset) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_pwrite(fdmap->dev, fdmap->drvfd, src, size, offset);
}

int eos_devfs_open(devfs_state_t *dstate, const char *path, int flags,
                   int mode) {
  eos_dev_t *dev = devfs_resolve(path);

  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  int drvfd = eos_drv_open(dev, "", flags, mode);

  if (drvfd < 0)
    return -1;

  return devfs_alloc_fd(dstate, dev, drvfd);
}

int eos_devfs_close(devfs_state_t *dstate, int fd) {
  for (size_t i = 0; i < kv_size(dstate->fds); ++i) {
    devfs_fdmap_t *fdmap = &kv_A(dstate->fds, i);

    if (fdmap->fd != fd)
      continue;

    int rc = eos_drv_close(fdmap->dev, fdmap->drvfd);

    /*
     * Keep the mapping if the driver failed to close.
     */
    if (rc < 0)
      return rc;

    /*
     * Order is irrelevant. The vector index is not the fd.
     */
    kv_drop_fast(devfs_fdmap_t, dstate->fds, i);

    return 0;
  }

  errno = EBADF;
  return -1;
}

int eos_devfs_fstat(devfs_state_t *dstate, int fd, struct stat *st) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_fstat(fdmap->dev, fdmap->drvfd, st);
}

int eos_devfs_fcntl(devfs_state_t *dstate, int fd, int cmd, int arg) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_fcntl(fdmap->dev, fdmap->drvfd, cmd, arg);
}

int eos_devfs_ioctl(devfs_state_t *dstate, int fd, int cmd, va_list args) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_ioctl(fdmap->dev, fdmap->drvfd, cmd, args);
}

int eos_devfs_fsync(devfs_state_t *dstate, int fd) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_fsync(fdmap->dev, fdmap->drvfd);
}

int eos_devfs_ftruncate(devfs_state_t *dstate, int fd, off_t length) {
  devfs_fdmap_t *fdmap = devfs_fd_to_fdmap(dstate, fd);
  if (!fdmap)
    return -1;

  return eos_drv_ftruncate(fdmap->dev, fdmap->drvfd, length);
}

///////////////////////////////////////////////////////////////////
// Path operations
///////////////////////////////////////////////////////////////////

int eos_devfs_stat(devfs_state_t *dstate, const char *path, struct stat *st) {
  (void)dstate;

  if (devfs_is_root(path)) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0755;
    return 0;
  }

  eos_dev_t *dev = devfs_resolve(path);

  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  return eos_drv_stat(dev, "", st);
}

int eos_devfs_link(devfs_state_t *dstate, const char *n1, const char *n2) {
  (void)dstate;

  return eos_drv_link(eos_devtree_root(), n1, n2);
}

int eos_devfs_unlink(devfs_state_t *dstate, const char *path) {
  (void)dstate;

  eos_dev_t *dev = devfs_resolve(path);

  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  return eos_drv_unlink(dev, "");
}

int eos_devfs_rename(devfs_state_t *dstate, const char *src, const char *dst) {
  (void)dstate;

  return eos_drv_rename(eos_devtree_root(), src, dst);
}

int eos_devfs_mkdir(devfs_state_t *dstate, const char *path, mode_t mode) {
  (void)dstate;

  if (path && *path == '/')
    path++;

  return eos_drv_mkdir(eos_devtree_root(), path, mode);
}

int eos_devfs_rmdir(devfs_state_t *dstate, const char *path) {
  (void)dstate;

  if (path && *path == '/')
    path++;

  return eos_drv_rmdir(eos_devtree_root(), path);
}

int eos_devfs_access(devfs_state_t *dstate, const char *path, int amode) {
  (void)dstate;

  if (devfs_is_root(path))
    return 0;

  eos_dev_t *dev = devfs_resolve(path);

  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  return eos_drv_access(dev, "", amode);
}

int eos_devfs_truncate(devfs_state_t *dstate, const char *path, off_t length) {
  (void)dstate;

  eos_dev_t *dev = devfs_resolve(path);

  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  return eos_drv_truncate(dev, "", length);
}

int eos_devfs_utime(devfs_state_t *dstate, const char *path,
                    const struct utimbuf *times) {
  (void)dstate;

  eos_dev_t *dev = devfs_resolve(path);

  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  return eos_drv_utime(dev, "", times);
}

///////////////////////////////////////////////////////////////////
// Directory operations
///////////////////////////////////////////////////////////////////

DIR *eos_devfs_opendir(devfs_state_t *dstate, const char *path) {
  (void)dstate;

  devfs_dir_t *dir = calloc(1, sizeof(*dir));

  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }

  /*
   * /dev is a flat listing of all attached devices.
   */
  if (devfs_is_root(path)) {
    dir->root_listing = true;
    return (DIR *)dir;
  }

  eos_dev_t *dev = devfs_resolve(path);

  if (!dev) {
    free(dir);
    errno = ENOENT;
    return NULL;
  }

  dir->dev = dev;

  /*
   * A device may be both a file and a directory.
   * The driver's opendir implementation decides whether it can
   * actually be opened as a directory.
   */
  dir->drvdir = eos_drv_opendir(dev, "");

  if (!dir->drvdir) {
    free(dir);
    return NULL;
  }

  return (DIR *)dir;
}

struct dirent *eos_devfs_readdir(devfs_state_t *dstate, DIR *pdir) {
  (void)dstate;

  devfs_dir_t *dir = (devfs_dir_t *)pdir;

  if (!dir) {
    errno = EINVAL;
    return NULL;
  }

  if (!dir->root_listing)
    return eos_drv_readdir(dir->dev, dir->drvdir);

  while (dir->index < EOS_MAX_DEVICES) {
    eos_dev_t *dev = &eos_devices[dir->index++];

    if (!dev->in_use)
      continue;

    if (dev->name[0] == '\0')
      continue;

    if (dev == eos_devtree_root())
      continue;

    memset(&dir->entry, 0, sizeof(dir->entry));

    dir->entry.d_ino = dev->id;

    dir->entry.d_type = (dev->driver && dev->driver->opendir) ? DT_DIR : DT_REG;

    strlcpy(dir->entry.d_name, dev->name, sizeof(dir->entry.d_name));

    return &dir->entry;
  }

  return NULL;
}

long eos_devfs_telldir(devfs_state_t *dstate, DIR *pdir) {
  (void)dstate;

  devfs_dir_t *dir = (devfs_dir_t *)pdir;

  if (!dir) {
    errno = EINVAL;
    return -1;
  }

  if (dir->root_listing)
    return (long)dir->index;

  return eos_drv_telldir(dir->dev, dir->drvdir);
}

void eos_devfs_seekdir(devfs_state_t *dstate, DIR *pdir, long offset) {
  (void)dstate;

  devfs_dir_t *dir = (devfs_dir_t *)pdir;

  if (!dir)
    return;

  if (dir->root_listing) {
    dir->index = (offset < 0) ? 0 : (uint32_t)offset;
    return;
  }

  eos_drv_seekdir(dir->dev, dir->drvdir, offset);
}

int eos_devfs_closedir(devfs_state_t *dstate, DIR *pdir) {
  (void)dstate;

  devfs_dir_t *dir = (devfs_dir_t *)pdir;

  if (!dir) {
    errno = EINVAL;
    return -1;
  }

  int rc = 0;

  if (!dir->root_listing) {
    rc = eos_drv_closedir(dir->dev, dir->drvdir);
  }

  free(dir);

  return rc;
}

///////////////////////////////////////////////////////////////////
// Mount / unmount
///////////////////////////////////////////////////////////////////

int eos_devfs_mount(const char *path, devfs_state_t *dstate) {
  if (!path || !dstate) {
    errno = EINVAL;
    return -1;
  }

  if (mkdir(path, 0755) < 0 && errno != EEXIST)
    return -1;

  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,

      .open_p = (void *)eos_devfs_open,
      .close_p = (void *)eos_devfs_close,

      .read_p = (void *)eos_devfs_read,
      .write_p = (void *)eos_devfs_write,
      .lseek_p = (void *)eos_devfs_lseek,

      .fstat_p = (void *)eos_devfs_fstat,
      .stat_p = (void *)eos_devfs_stat,

      .link_p = (void *)eos_devfs_link,
      .unlink_p = (void *)eos_devfs_unlink,
      .rename_p = (void *)eos_devfs_rename,

      .opendir_p = (void *)eos_devfs_opendir,
      .readdir_p = (void *)eos_devfs_readdir,
      .telldir_p = (void *)eos_devfs_telldir,
      .seekdir_p = (void *)eos_devfs_seekdir,
      .closedir_p = (void *)eos_devfs_closedir,

      .mkdir_p = (void *)eos_devfs_mkdir,
      .rmdir_p = (void *)eos_devfs_rmdir,

      .fcntl_p = (void *)eos_devfs_fcntl,
      .ioctl_p = (void *)eos_devfs_ioctl,

      .fsync_p = (void *)eos_devfs_fsync,
      .access_p = (void *)eos_devfs_access,

      .truncate_p = (void *)eos_devfs_truncate,
      .ftruncate_p = (void *)eos_devfs_ftruncate,

      .utime_p = (void *)eos_devfs_utime,
  };

  esp_vfs_register(path, &vfs, dstate);

  return 0;
}

int eos_devfs_unmount(const char *path, devfs_state_t *dstate) {
  if (!path || !dstate) {
    errno = EINVAL;
    return -1;
  }

  if (kv_size(dstate->fds) != 0) {
    errno = EBUSY;
    return -1;
  }

  return esp_vfs_unregister(path);
}

///////////////////////////////////////////////////////////////////
// Init
///////////////////////////////////////////////////////////////////

void eos_devfs_init(void) {
  if (eos_devfs_state)
    return;

  eos_devfs_state = calloc(1, sizeof(*eos_devfs_state));

  if (!eos_devfs_state) {
    EOS_LOGE("Failed to allocate devfs state\n");
    return;
  }

  kv_init(eos_devfs_state->fds);

  if (eos_devfs_mount(EOS_DEVFS_ROOT, eos_devfs_state) < 0) {
    kv_destroy(eos_devfs_state->fds);
    free(eos_devfs_state);
    eos_devfs_state = NULL;
    return;
  }

  // Create a dir on tmpfs
  mkdir(EOS_DEVFS_ROOT, 0755);

  EOS_LOGI("devfs mounted at %s\n", EOS_DEVFS_ROOT);
}
