#include "devfs.h"
#include "driver/driver.h"
#include "misc/fancymacro.h"
#include "sys/device.h"
#include <dirent.h>
#include <errno.h>
#include <esp_vfs.h>
#include <stdlib.h>
#include <string.h>

// ── Internal: fd → device ─────────────────────────────────────

static eos_dev_t *eos_devfs_fd_to_dev(int fd) {
  for (uint32_t i = 0; i < EOS_MAX_DEVICES; i++)
    if (eos_devices[i].in_use && eos_devices[i].fd == fd)
      return &eos_devices[i];
  return NULL;
}

// ── Internal: path → device ───────────────────────────────────

// Flat lookup — find leaf device by name directly
static eos_dev_t *eos_devfs_path_to_dev(const char *path) {
  if (!path)
    return NULL;

  const char *name = path;
  if (*name == '/')
    name++;
  if (*name == '\0')
    return NULL;

  for (uint32_t i = 0; i < EOS_MAX_DEVICES; i++) {
    eos_dev_t *dev = &eos_devices[i];
    if (!dev->in_use)
      continue;
    if (!dev->driver)
      continue;
    if (dev->child)
      continue; // buses not openable
    if (strcmp(dev->name, name) == 0)
      return dev;
  }
  return NULL;
}

// ── Dir state ─────────────────────────────────────────────────

typedef struct {
  uint32_t esp_idf_fs_index;
  uint32_t dev_idx;
  struct dirent entry;
} devfs_dir_t;

// ── ESP-IDF VFS callbacks ─────────────────────────────────────

static int devfs_open(void *ctx, const char *path, int flags, int mode) {
  eos_dev_t *dev = eos_devfs_path_to_dev(path);
  if (!dev) {
    errno = ENOENT;
    return -1;
  }
  if (dev->child) {
    errno = EISDIR;
    return -1;
  } // buses not openable
  if (dev->fd >= 0) {
    errno = EBUSY;
    return -1;
  }

  int fd = (int)(dev - eos_devices);
  dev->fd = fd;
  return fd;
}

static int devfs_close(void *ctx, int fd) {
  eos_dev_t *dev = eos_devfs_fd_to_dev(fd);
  if (!dev) {
    errno = EBADF;
    return -1;
  }
  dev->fd = -1;
  return 0;
}

static ssize_t devfs_read(void *ctx, int fd, void *buf, size_t len) {
  eos_dev_t *dev = eos_devfs_fd_to_dev(fd);
  if (!dev) {
    errno = EBADF;
    return -1;
  }
  return dev->driver->read(dev, buf, len);
}

static ssize_t devfs_write(void *ctx, int fd, const void *buf, size_t len) {
  eos_dev_t *dev = eos_devfs_fd_to_dev(fd);
  if (!dev) {
    errno = EBADF;
    return -1;
  }
  return dev->driver->write(dev, (void *)buf, len);
}

static int devfs_ioctl(void *ctx, int fd, int cmd, va_list args) {
  eos_dev_t *dev = eos_devfs_fd_to_dev(fd);
  if (!dev) {
    errno = EBADF;
    return -1;
  }
  return dev->driver->ioctl(dev, cmd, args);
}

static off_t devfs_lseek(void *ctx, int fd, off_t offset, int whence) {
  eos_dev_t *dev = eos_devfs_fd_to_dev(fd);
  if (!dev) {
    errno = EBADF;
    return -1;
  }
  return dev->driver->lseek(dev, offset, whence);
}

static DIR *devfs_opendir(void *ctx, const char *path) {
  devfs_dir_t *dir = malloc(sizeof(devfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }
  dir->dev_idx = 0;
  return (DIR *)dir;
}

static struct dirent *devfs_readdir(void *ctx, DIR *pdir) {
  devfs_dir_t *dir = (devfs_dir_t *)pdir;

  // walk flat eos_devices[], yield only leaf devices
  while (dir->dev_idx < EOS_MAX_DEVICES) {
    eos_dev_t *dev = &eos_devices[dir->dev_idx++];
    if (!dev->in_use)
      continue;
    if (!dev->driver)
      continue;
    if (dev->child)
      continue; // skip buses
    if (!dev->parent)
      continue; // skip root

    memset(&dir->entry, 0, sizeof(dir->entry));
    dir->entry.d_ino = dir->dev_idx - 1;
    dir->entry.d_type = DT_CHR;
    strlcpy(dir->entry.d_name, dev->name, EOS_SMALL_STR_LEN);
    return &dir->entry;
  }

  return NULL;
}

static void devfs_seekdir(void *ctx, DIR *pdir, long offset) {
  devfs_dir_t *dir = (devfs_dir_t *)pdir;
  dir->dev_idx = (uint32_t)offset;
}

static long devfs_telldir(void *ctx, DIR *pdir) {
  devfs_dir_t *dir = (devfs_dir_t *)pdir;
  return (long)dir->dev_idx;
}

static int devfs_closedir(void *ctx, DIR *pdir) {
  free(pdir);
  return 0;
}

// ── Init ──────────────────────────────────────────────────────

void eos_devfs_init() {
  for (uint32_t i = 0; i < EOS_MAX_DEVICES; i++)
    if (eos_devices[i].in_use)
      eos_devices[i].fd = -1;

  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,
      .open_p = devfs_open,
      .close_p = devfs_close,
      .read_p = devfs_read,
      .write_p = devfs_write,
      .ioctl_p = devfs_ioctl,
      .lseek_p = devfs_lseek,
      .opendir_p = devfs_opendir,
      .readdir_p = devfs_readdir,
      .seekdir_p = devfs_seekdir,
      .telldir_p = devfs_telldir,
      .closedir_p = devfs_closedir,
  };

  esp_vfs_register(EOS_DEVFS_ROOT, &vfs, NULL);
  EOS_LOGI("devfs mounted at %s", EOS_DEVFS_ROOT);
}