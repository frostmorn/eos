#include "devfs.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/proc.h"
#include "ecore/vfs.h"
#include "emisc/fancymacro.h"
#include "emisc/psram_alloc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  eos_vfs_file_t base;
  eos_dev_t *dev;
  int drvfd;
  bool is_null;
  bool is_zero;
} devfs_file_t;

typedef struct {
  eos_vfs_dir_t base;
  bool root_listing;
  uint32_t index;
  eos_dev_t *dev;
  DIR *drvdir;
  struct dirent entry;
} devfs_dir_t;

static eos_dev_t *devfs_resolve(const char *path) {
  if (!path)
    return NULL;

  while (*path == '/')
    path++;

  if (*path == '\0')
    return NULL;

  return eos_dev_find_by_name(path);
}

static bool devfs_is_root(const char *path) {
  if (!path || *path == '\0')
    return true;

  while (*path == '/')
    path++;

  return *path == '\0';
}

static int devfs_open(void *ctx, const char *path, int flags, int mode, eos_vfs_file_t **out_file) {
  (void)ctx;
  if (!path) {
    errno = EINVAL;
    return -1;
  }

  while (*path == '/')
    path++;

  const char *slash = strchr(path, '/');
  char devname[EOS_XSMALL_STR_LEN];

  if (slash) {
    size_t len = slash - path;
    if (len == 0 || len >= sizeof(devname)) {
      errno = ENAMETOOLONG;
      return -1;
    }
    memcpy(devname, path, len);
    devname[len] = '\0';
  } else {
    strlcpy(devname, path, sizeof(devname));
  }

  if (strcmp(devname, "null") == 0 || strcmp(devname, "zero") == 0) {
    if (slash) {
      errno = ENOTDIR;
      return -1;
    }
    devfs_file_t *file = eos_vfs_file_alloc(NULL, sizeof(devfs_file_t), flags);
    if (!file) {
      errno = ENOMEM;
      return -1;
    }
    file->dev = NULL;
    file->drvfd = -1;
    file->is_null = (strcmp(devname, "null") == 0);
    file->is_zero = (strcmp(devname, "zero") == 0);
    *out_file = &file->base;
    return file->base.fd;
  }

  eos_dev_t *dev = eos_dev_find_by_name(devname);
  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  int drvfd = eos_dev_open(dev, slash ? slash : "", flags, mode);
  if (drvfd < 0) {
    return -1;
  }

  devfs_file_t *file = eos_vfs_file_alloc(NULL, sizeof(devfs_file_t), flags);
  if (!file) {
    eos_dev_close(dev, drvfd);
    errno = ENOMEM;
    return -1;
  }

  file->dev = dev;
  file->drvfd = drvfd;
  *out_file = &file->base;
  return file->base.fd;
}

static int devfs_close(void *ctx, eos_vfs_file_t *file) {
  (void)ctx;
  if (!file) return 0;

  devfs_file_t *df = (devfs_file_t *)file;
  if (df->is_null || df->is_zero) {
    return 0;
  }
  if (df->dev) {
    return eos_dev_close(df->dev, df->drvfd);
  }
  return 0;
}

static ssize_t devfs_read(void *ctx, eos_vfs_file_t *file, void *dst, size_t size) {
  (void)ctx;
  devfs_file_t *df = (devfs_file_t *)file;
  if (!df) {
    errno = EBADF;
    return -1;
  }
  if (df->is_null) {
    return 0;
  }
  if (df->is_zero) {
    if (dst && size > 0) {
      memset(dst, 0, size);
    }
    return (ssize_t)size;
  }
  if (!df->dev || !df->dev->in_use) {
    errno = ENODEV;
    return -1;
  }
  return eos_dev_read(df->dev, df->drvfd, dst, size);
}

static ssize_t devfs_write(void *ctx, eos_vfs_file_t *file, const void *src, size_t size) {
  (void)ctx;
  devfs_file_t *df = (devfs_file_t *)file;
  if (!df) {
    errno = EBADF;
    return -1;
  }
  if (df->is_null || df->is_zero) {
    (void)src;
    return (ssize_t)size;
  }
  if (!df->dev || !df->dev->in_use) {
    errno = ENODEV;
    return -1;
  }
  return eos_dev_write(df->dev, df->drvfd, src, size);
}

static off_t devfs_lseek(void *ctx, eos_vfs_file_t *file, off_t offset, int whence) {
  (void)ctx;
  devfs_file_t *df = (devfs_file_t *)file;
  if (!df) {
    errno = EBADF;
    return -1;
  }
  if (df->is_null || df->is_zero) {
    (void)offset;
    (void)whence;
    return 0;
  }
  if (!df->dev || !df->dev->in_use) {
    errno = ENODEV;
    return -1;
  }
  return eos_dev_lseek(df->dev, df->drvfd, offset, whence);
}

static int devfs_fstat(void *ctx, eos_vfs_file_t *file, struct stat *st) {
  (void)ctx;
  if (!st) {
    errno = EINVAL;
    return -1;
  }
  devfs_file_t *df = (devfs_file_t *)file;
  if (!df) {
    errno = EBADF;
    return -1;
  }
  if (df->is_null || df->is_zero) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0666;
    return 0;
  }
  if (!df->dev || !df->dev->in_use) {
    errno = ENODEV;
    return -1;
  }
  return eos_drv_fstat(df->dev, df->drvfd, st);
}

static int devfs_stat(void *ctx, const char *path, struct stat *st) {
  (void)ctx;
  if (!path || !st) {
    errno = EINVAL;
    return -1;
  }

  if (devfs_is_root(path)) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0755;
    return 0;
  }

  const char *p = path;
  while (*p == '/') p++;
  if (strcmp(p, "null") == 0 || strcmp(p, "zero") == 0) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0666;
    return 0;
  }

  eos_dev_t *dev = devfs_resolve(path);
  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  return eos_drv_stat(dev, "", st);
}

static int devfs_link(void *ctx, const char *n1, const char *n2) {
  (void)ctx;
  return eos_drv_link(eos_devtree_root(), n1, n2);
}

static int devfs_unlink(void *ctx, const char *path) {
  (void)ctx;
  const char *p = path;
  while (*p && *p == '/') p++;
  if (strcmp(p, "null") == 0 || strcmp(p, "zero") == 0) {
    errno = EPERM;
    return -1;
  }
  eos_dev_t *dev = devfs_resolve(path);
  if (!dev) {
    errno = ENOENT;
    return -1;
  }
  return eos_drv_unlink(dev, "");
}

static int devfs_rename(void *ctx, const char *src, const char *dst) {
  (void)ctx;
  return eos_drv_rename(eos_devtree_root(), src, dst);
}

static int devfs_mkdir(void *ctx, const char *path, mode_t mode) {
  (void)ctx;
  if (path && *path == '/')
    path++;
  return eos_drv_mkdir(eos_devtree_root(), path, mode);
}

static int devfs_rmdir(void *ctx, const char *path) {
  (void)ctx;
  if (path && *path == '/')
    path++;
  return eos_drv_rmdir(eos_devtree_root(), path);
}

static int devfs_access(void *ctx, const char *path, int amode) {
  (void)ctx;
  if (devfs_is_root(path))
    return 0;

  const char *p = path;
  while (*p == '/') p++;
  if (strcmp(p, "null") == 0 || strcmp(p, "zero") == 0) {
    return 0;
  }

  eos_dev_t *dev = devfs_resolve(path);
  if (!dev) {
    errno = ENOENT;
    return -1;
  }

  return eos_drv_access(dev, "", amode);
}

static int devfs_ioctl(void *ctx, eos_vfs_file_t *file, int cmd, va_list args) {
  (void)ctx;
  (void)cmd;
  (void)args;
  devfs_file_t *df = (devfs_file_t *)file;
  if (!df) {
    errno = EBADF;
    return -1;
  }
  if (df->is_null || df->is_zero) {
    return 0;
  }
  if (!df->dev || !df->dev->in_use) {
    errno = ENODEV;
    return -1;
  }

  bool prev_k = eos_proc_enter_kernel();
  int ret = eos_drv_ioctl(df->dev, df->drvfd, cmd, args);
  eos_proc_leave_kernel(prev_k);
  return ret;
}

static int devfs_opendir(void *ctx, const char *path, eos_vfs_dir_t **out_dir) {
  (void)ctx;
  bool is_root = devfs_is_root(path);
  eos_dev_t *dev = NULL;
  DIR *drvdir = NULL;

  if (!is_root) {
    dev = devfs_resolve(path);
    if (!dev) {
      errno = ENOENT;
      return -1;
    }
    drvdir = eos_dev_opendir(dev, "");
    if (!drvdir) {
      return -1;
    }
  }

  devfs_dir_t *dir = eos_vfs_dir_alloc(NULL, sizeof(devfs_dir_t));
  if (!dir) {
    if (drvdir) {
      eos_dev_closedir(dev, drvdir);
    }
    errno = ENOMEM;
    return -1;
  }

  dir->root_listing = is_root;
  dir->dev = dev;
  dir->drvdir = drvdir;
  dir->index = 0;
  *out_dir = &dir->base;
  return 0;
}

static struct dirent *devfs_readdir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  devfs_dir_t *dir = (devfs_dir_t *)pdir;
  if (!dir) {
    errno = EINVAL;
    return NULL;
  }

  if (!dir->root_listing) {
    if (!dir->dev || !dir->drvdir) {
      return NULL;
    }
    return eos_dev_readdir(dir->dev, dir->drvdir);
  }

  if (dir->index == 0) {
    dir->index++;
    memset(&dir->entry, 0, sizeof(dir->entry));
    dir->entry.d_ino = 1;
    dir->entry.d_type = DT_CHR;
    strlcpy(dir->entry.d_name, "null", sizeof(dir->entry.d_name));
    return &dir->entry;
  }

  if (dir->index == 1) {
    dir->index++;
    memset(&dir->entry, 0, sizeof(dir->entry));
    dir->entry.d_ino = 2;
    dir->entry.d_type = DT_CHR;
    strlcpy(dir->entry.d_name, "zero", sizeof(dir->entry.d_name));
    return &dir->entry;
  }

  uint32_t dev_idx = dir->index - 2;
  while (dev_idx < eos_dev_count()) {
    eos_dev_t *dev = eos_dev_get(dev_idx++);
    dir->index = dev_idx + 2;
    if (!dev || !dev->in_use)
      continue;

    if (!dev->driver)
      continue;

    if (dev->driver->flags & EOS_DRV_FLAG_NO_VFS)
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

static int devfs_closedir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  devfs_dir_t *dir = (devfs_dir_t *)pdir;
  if (!dir) {
    errno = EINVAL;
    return -1;
  }

  int rc = 0;
  if (!dir->root_listing && dir->dev && dir->drvdir) {
    rc = eos_dev_closedir(dir->dev, dir->drvdir);
  }
  return rc;
}

static void devfs_seekdir(void *ctx, eos_vfs_dir_t *pdir, long loc) {
  (void)ctx;
  devfs_dir_t *dir = (devfs_dir_t *)pdir;
  if (!dir) return;
  if (!dir->root_listing) {
    if (dir->dev && dir->drvdir) {
      eos_dev_seekdir(dir->dev, dir->drvdir, loc);
    }
  } else {
    dir->index = (uint32_t)loc;
  }
}

static long devfs_telldir(void *ctx, eos_vfs_dir_t *pdir) {
  (void)ctx;
  devfs_dir_t *dir = (devfs_dir_t *)pdir;
  if (!dir) return -1;
  if (!dir->root_listing) {
    return -1;
  }
  return (long)dir->index;
}

void eos_devfs_init(void) {
  static const eos_vfs_ops_t ops = {
      .open = devfs_open,
      .close = devfs_close,
      .read = devfs_read,
      .write = devfs_write,
      .lseek = devfs_lseek,
      .fstat = devfs_fstat,
      .stat = devfs_stat,
      .link = devfs_link,
      .unlink = devfs_unlink,
      .rename = devfs_rename,
      .mkdir = devfs_mkdir,
      .rmdir = devfs_rmdir,
      .access = devfs_access,
      .ioctl = devfs_ioctl,
      .opendir = devfs_opendir,
      .readdir = devfs_readdir,
      .closedir = devfs_closedir,
      .seekdir = devfs_seekdir,
      .telldir = devfs_telldir,
  };

  mkdir(EOS_DEVFS_ROOT, 0755);
  eos_vfs_register_ops(EOS_DEVFS_ROOT, &ops, NULL);
  EOS_LOGI("devfs mounted at %s\n", EOS_DEVFS_ROOT);
}
