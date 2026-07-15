#include "devfs.h"
#include "misc/fancymacro.h"
#include "sys/device.h"
#include "sys/driver.h"
#include <dirent.h>
#include <errno.h>
#include <esp_vfs.h>
#include <stdlib.h>
#include <string.h>

// ── Internal: fd → device ─────────────────────────────────────

// fd stored directly on a device itself, which limits usage of device to one
// user, completely okie
static eos_dev_t *eos_devfs_fd_to_dev(int fd) {
  for (uint32_t i = 0; i < EOS_MAX_DEVICES; i++)
    if (eos_devices[i].in_use && eos_devices[i].fd == fd)
      return &eos_devices[i];
  return NULL;
}

// ── Internal: path → device ───────────────────────────────────

// Traversal by devtree, seeking for a device
static eos_dev_t *eos_devfs_path_to_dev(const char *path) {
  if (!path)
    return NULL;

  // esp_vfs passes path with prefix stripped — starts with '/' or empty
  const char *rest = path;
  if (*rest == '/')
    rest++;
  if (*rest == '\0')
    return eos_devtree_root();

  eos_dev_t *cur = eos_devtree_root()->child;

  while (cur && *rest) {
    // extract next segment
    char segment[EOS_SMALL_STR_LEN];
    const char *slash = strchr(rest, '/');
    if (slash) {
      size_t seg_len = slash - rest;
      strlcpy(segment, rest, seg_len + 1);
      rest = slash + 1;
    } else {
      strlcpy(segment, rest, sizeof(segment));
      rest += strlen(rest);
    }

    // find sibling matching segment
    eos_dev_t *match = NULL;
    eos_dev_t *sib = cur;
    while (sib) {
      if (strcmp(sib->name, segment) == 0) {
        match = sib;
        break;
      }
      sib = sib->next;
    }

    if (!match)
      return NULL;
    if (*rest == '\0')
      return match;
    cur = match->child;
  }

  return NULL;
}

// ── Internal: device → path ───────────────────────────────────
// TODo: probably we do not really need it at all, maybe save it, commented out
// temporary static void eos_devfs_dev_to_path(eos_dev_t *dev, char *buf, size_t
// len) {
//   if (!dev || !buf || len == 0)
//     return;

//   eos_dev_t *nodes[EOS_MAX_DEVICES];
//   uint32_t depth = 0;

//   eos_dev_t *cur = dev;
//   while (cur && cur->parent) {
//     nodes[depth++] = cur;
//     cur = cur->parent;
//   }

//   buf[0] = '\0';
//   strlcat(buf, EOS_DEVFS_ROOT, len);

//   for (int32_t i = depth - 1; i >= 0; i--) {
//     strlcat(buf, "/", len);
//     strlcat(buf, nodes[i]->name, len);
//   }
// }

// ── Dir state ─────────────────────────────────────────────────

typedef struct {
  eos_dev_t *cur;
  struct dirent entry;
} devfs_dir_t;

// ── ESP-IDF VFS callbacks ─────────────────────────────────────

static int devfs_open(void *ctx, const char *path, int flags, int mode) {
  eos_dev_t *dev = eos_devfs_path_to_dev(path);
  if (!dev) {
    errno = ENOENT;
    return -1;
  }
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
  if (!dev->driver->lseek) {
    errno = ESPIPE;
    return -1;
  }
  return dev->driver->lseek(dev, offset, whence);
}

static DIR *devfs_opendir(void *ctx, const char *path) {
  eos_dev_t *dev = eos_devfs_path_to_dev(path);
  if (!dev) {
    errno = ENOENT;
    return NULL;
  }

  devfs_dir_t *dir = malloc(sizeof(devfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }

  dir->cur = dev->child;
  return (DIR *)dir;
}

// TODO: replace with static allocated dirent list
// maybe store it on a device itself instead of storing just name
// if struct entry.d_type == DT_DIR, it would mean that driver have own opendir
// sharing own files, otherwise it should work as DT_CHR
static struct dirent *devfs_readdir(void *ctx, DIR *pdir) {
  devfs_dir_t *dir = (devfs_dir_t *)pdir;
  if (!dir->cur)
    return NULL;

  strlcpy(dir->entry.d_name, dir->cur->name, sizeof(dir->entry.d_name));
  dir->entry.d_type = dir->cur->child ? DT_DIR : DT_CHR;

  dir->cur = dir->cur->next;
  return &dir->entry;
}

static int devfs_closedir(void *ctx, DIR *pdir) {
  free(pdir);
  return 0;
}

// ── Init ──────────────────────────────────────────────────────

void eos_devfs_init() {
  // Initialize all device fds to -1
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
      .closedir_p = devfs_closedir,
  };

  esp_vfs_register(EOS_DEVFS_ROOT, &vfs, NULL);
  EOS_LOGI("devfs mounted at %s", EOS_DEVFS_ROOT);
}