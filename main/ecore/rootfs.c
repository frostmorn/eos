#include "emisc/fancymacro.h"
#include "rootfs.h"
#include <dirent.h>
#include <errno.h>
#include <esp_vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// (^__^)==\~ HECK_STORY_BEGIN 

// yeah yeah, we do know those exist

// Consult espressif/components/vfs/private_include/esp_vfs_private.h if api changes
// Current implementation provided for espressif 6.0.2 version

typedef struct vfs_entry_ {
    int flags;                   /*!< ESP_VFS_FLAG_CONTEXT_PTR and/or ESP_VFS_FLAG_READONLY_FS or ESP_VFS_FLAG_DEFAULT */
    const esp_vfs_fs_ops_t *vfs; /*!< contains pointers to VFS functions */
    void *ctx;                   /*!< optional pointer which can be passed to VFS */
    int offset;                  /*!< index of this structure in s_vfs array */
    size_t path_prefix_len;      /*!< micro-optimization to avoid doing extra strlen, contains length of the string, not the size of path_prefix array */
    const char path_prefix[]; /*!< path prefix mapped to this VFS */
} vfs_entry_t;

extern vfs_entry_t * s_vfs;
extern size_t s_vfs_count;

// HECK_STORY_END (^__^)==\~

// ── Dir state ─────────────────────────────────────────────────

typedef struct {
  uint32_t esp_idf_fs_index; // must be first
  int idx;                   // current position in path list
} rootfs_dir_t;

// ── VFS callbacks ─────────────────────────────────────────────

static int rootfs_open(void *ctx, const char *path, int flags, int mode) {
  // root filesystem has no openable files — only directories
  errno = EISDIR;
  return -1;
}

static int rootfs_close(void *ctx, int fd) {
  errno = EBADF;
  return -1;
}

static ssize_t rootfs_read(void *ctx, int fd, void *buf, size_t len) {
  errno = EBADF;
  return -1;
}

static ssize_t rootfs_write(void *ctx, int fd, const void *buf, size_t len) {
  errno = EROFS;
  return -1;
}

static int rootfs_stat(void *ctx, const char *path, struct stat *st) {
  if (!st) {
    errno = EINVAL;
    return -1;
  }

  // root itself
  if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    return 0;
  }

  // TODO:

  errno = ENOENT;
  return -1;
}

static DIR *rootfs_opendir(void *ctx, const char *path) {
  if (strcmp(path, "/") != 0 && strcmp(path, "") != 0) {
    errno = EINVAL;
    return NULL;
  }

  rootfs_dir_t *dir = malloc(sizeof(rootfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }

  dir->idx = 0;
  return (DIR *)dir;
}

static struct dirent *rootfs_readdir(void *ctx, DIR *pdir) {
  rootfs_dir_t *dir = (rootfs_dir_t *)pdir;

  static struct dirent s_entry;

  // TODO:

  return NULL; // end of list
}

static int rootfs_closedir(void *ctx, DIR *pdir) {
  if (!pdir) {
    errno = EINVAL;
    return -1;
  }
  free(pdir);
  return 0;
}

static void rootfs_seekdir(void *ctx, DIR *pdir, long offset) {
  rootfs_dir_t *dir = (rootfs_dir_t *)pdir;
  dir->idx = (int)offset;
}

static long rootfs_telldir(void *ctx, DIR *pdir) {
  rootfs_dir_t *dir = (rootfs_dir_t *)pdir;
  return (long)dir->idx;
}

// ── Init ──────────────────────────────────────────────────────

void eos_rootfs_init(void) {
  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,
      .open_p = rootfs_open,
      .close_p = rootfs_close,
      .read_p = rootfs_read,
      .write_p = rootfs_write,
      .stat_p = rootfs_stat,
      .opendir_p = rootfs_opendir,
      .readdir_p = rootfs_readdir,
      .closedir_p = rootfs_closedir,
      .seekdir_p = rootfs_seekdir,
      .telldir_p = rootfs_telldir,
  };

  esp_vfs_register("", &vfs, NULL);
  EOS_LOGI("rootfs: mounted at ");
}
