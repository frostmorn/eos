#include "efs/fat/fat.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/proc.h"
#include "ecore/vfs.h"
#include "emisc/psram_alloc.h"
#include "emisc/strlimits.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ecore/vfs_real.h"

typedef struct {
  char prefix[EOS_SMALL_STR_LEN];
  eos_dev_t *dev;
} fat_mount_ctx_t;

static void fat_full_path(char *dst, size_t dst_sz, const char *prefix, const char *rel_path) {
  if (!rel_path || rel_path[0] == '\0') {
    strlcpy(dst, prefix, dst_sz);
  } else if (rel_path[0] == '/') {
    snprintf(dst, dst_sz, "%s%s", prefix, rel_path);
  } else {
    snprintf(dst, dst_sz, "%s/%s", prefix, rel_path);
  }
}

static int fat_vfs_open(void *ctx, const char *rel_path, int flags, int mode, eos_vfs_file_t **out_file) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full[PATH_MAX];
  fat_full_path(full, sizeof(full), fc->prefix, rel_path);

  bool prev_h = eos_proc_heap_tracking_disable();
  int lfd = __real__open_r(__getreent(), full, flags, mode);
  eos_proc_heap_tracking_restore(prev_h);
  if (lfd < 0) return -1;

  eos_vfs_file_t *file = eos_vfs_file_alloc(NULL, sizeof(eos_vfs_file_t), flags);
  if (!file) {
    bool ph = eos_proc_heap_tracking_disable();
    __real__close_r(__getreent(), lfd);
    eos_proc_heap_tracking_restore(ph);
    errno = ENOMEM;
    return -1;
  }
  file->fs_data = (void *)(intptr_t)lfd;
  *out_file = file;
  return file->fd;
}

static int fat_vfs_close(void *ctx, eos_vfs_file_t *file) {
  (void)ctx;
  if (!file) return 0;
  int lfd = (int)(intptr_t)file->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real__close_r(__getreent(), lfd);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static ssize_t fat_vfs_read(void *ctx, eos_vfs_file_t *file, void *dst, size_t size) {
  (void)ctx;
  if (!file) return -1;
  int lfd = (int)(intptr_t)file->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  ssize_t rc = __real__read_r(__getreent(), lfd, dst, size);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static ssize_t fat_vfs_write(void *ctx, eos_vfs_file_t *file, const void *src, size_t size) {
  (void)ctx;
  if (!file) return -1;
  int lfd = (int)(intptr_t)file->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  ssize_t rc = __real__write_r(__getreent(), lfd, (void *)src, size);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static off_t fat_vfs_lseek(void *ctx, eos_vfs_file_t *file, off_t offset, int whence) {
  (void)ctx;
  if (!file) return -1;
  int lfd = (int)(intptr_t)file->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  off_t rc = __real__lseek_r(__getreent(), lfd, offset, whence);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_fstat(void *ctx, eos_vfs_file_t *file, struct stat *st) {
  (void)ctx;
  if (!file) return -1;
  int lfd = (int)(intptr_t)file->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real__fstat_r(__getreent(), lfd, st);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_stat(void *ctx, const char *rel_path, struct stat *st) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full[PATH_MAX];
  fat_full_path(full, sizeof(full), fc->prefix, rel_path);
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real__stat_r(__getreent(), full, st);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_unlink(void *ctx, const char *rel_path) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full[PATH_MAX];
  fat_full_path(full, sizeof(full), fc->prefix, rel_path);
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real__unlink_r(__getreent(), full);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_rename(void *ctx, const char *rel_src, const char *rel_dst) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full_src[PATH_MAX], full_dst[PATH_MAX];
  fat_full_path(full_src, sizeof(full_src), fc->prefix, rel_src);
  fat_full_path(full_dst, sizeof(full_dst), fc->prefix, rel_dst);
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real__rename_r(__getreent(), full_src, full_dst);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_mkdir(void *ctx, const char *rel_path, mode_t mode) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full[PATH_MAX];
  fat_full_path(full, sizeof(full), fc->prefix, rel_path);
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real_mkdir(full, mode);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_rmdir(void *ctx, const char *rel_path) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full[PATH_MAX];
  fat_full_path(full, sizeof(full), fc->prefix, rel_path);
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real_rmdir(full);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_access(void *ctx, const char *rel_path, int amode) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full[PATH_MAX];
  fat_full_path(full, sizeof(full), fc->prefix, rel_path);
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real_access(full, amode);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static int fat_vfs_opendir(void *ctx, const char *rel_path, eos_vfs_dir_t **out_dir) {
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;
  char full[PATH_MAX];
  fat_full_path(full, sizeof(full), fc->prefix, rel_path);
  bool prev_h = eos_proc_heap_tracking_disable();
  DIR *rdir = __real_opendir(full);
  eos_proc_heap_tracking_restore(prev_h);
  if (!rdir) return -1;

  eos_vfs_dir_t *dir = eos_vfs_dir_alloc(NULL, sizeof(eos_vfs_dir_t));
  if (!dir) {
    bool ph = eos_proc_heap_tracking_disable();
    __real_closedir(rdir);
    eos_proc_heap_tracking_restore(ph);
    errno = ENOMEM;
    return -1;
  }
  dir->fs_data = rdir;
  *out_dir = dir;
  return 0;
}

static struct dirent *fat_vfs_readdir(void *ctx, eos_vfs_dir_t *dir) {
  (void)ctx;
  if (!dir || !dir->fs_data) return NULL;
  DIR *rdir = (DIR *)dir->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  struct dirent *de = __real_readdir(rdir);
  eos_proc_heap_tracking_restore(prev_h);
  return de;
}

static int fat_vfs_closedir(void *ctx, eos_vfs_dir_t *dir) {
  (void)ctx;
  if (!dir || !dir->fs_data) return 0;
  DIR *rdir = (DIR *)dir->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  int rc = __real_closedir(rdir);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static void fat_vfs_seekdir(void *ctx, eos_vfs_dir_t *dir, long loc) {
  (void)ctx;
  if (!dir || !dir->fs_data) return;
  DIR *rdir = (DIR *)dir->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  __real_seekdir(rdir, loc);
  eos_proc_heap_tracking_restore(prev_h);
}

static long fat_vfs_telldir(void *ctx, eos_vfs_dir_t *dir) {
  (void)ctx;
  if (!dir || !dir->fs_data) return -1;
  DIR *rdir = (DIR *)dir->fs_data;
  bool prev_h = eos_proc_heap_tracking_disable();
  long rc = __real_telldir(rdir);
  eos_proc_heap_tracking_restore(prev_h);
  return rc;
}

static const eos_vfs_ops_t s_fat_vfs_ops = {
    .open = fat_vfs_open,
    .close = fat_vfs_close,
    .read = fat_vfs_read,
    .write = fat_vfs_write,
    .lseek = fat_vfs_lseek,
    .fstat = fat_vfs_fstat,
    .stat = fat_vfs_stat,
    .link = NULL,
    .unlink = fat_vfs_unlink,
    .rename = fat_vfs_rename,
    .mkdir = fat_vfs_mkdir,
    .rmdir = fat_vfs_rmdir,
    .access = fat_vfs_access,
    .ioctl = NULL,
    .opendir = fat_vfs_opendir,
    .readdir = fat_vfs_readdir,
    .closedir = fat_vfs_closedir,
    .seekdir = fat_vfs_seekdir,
    .telldir = fat_vfs_telldir,
    .symlink = NULL,
    .readlink = NULL,
    .lstat = fat_vfs_stat,
};

int eos_fat_mount(const char *source, const char *target, uint32_t flags, const char *opts, void **out_ctx) {
  (void)flags; (void)opts;
  if (!source || !target) return -1;
  const char *dev_name = source;
  if (strncmp(dev_name, "/dev/", 5) == 0) dev_name += 5;
  eos_dev_t *dev = eos_dev_find_by_name(dev_name);
  if (!dev) return -1;

  bool result = false;
  int rc = eos_dev_ioctl_call(dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_MOUNT, target, &result);
  if (rc != 0 || !result) return -1;

  fat_mount_ctx_t *fc = eos_psram_calloc_prefer(1, sizeof(fat_mount_ctx_t));
  if (!fc) {
    (void)eos_dev_ioctl_call(dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_UMOUNT, &result);
    return -1;
  }
  strlcpy(fc->prefix, target, sizeof(fc->prefix));
  fc->dev = dev;

  esp_err_t err = eos_vfs_register_ops(target, &s_fat_vfs_ops, fc);
  if (err != ESP_OK) {
    (void)eos_dev_ioctl_call(dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_UMOUNT, &result);
    eos_psram_free(fc);
    return -1;
  }

  if (out_ctx) *out_ctx = fc;
  return 0;
}

int eos_fat_umount(const char *target, void *ctx, uint32_t flags) {
  (void)flags;
  if (!ctx && !target) return -1;
  fat_mount_ctx_t *fc = (fat_mount_ctx_t *)ctx;

  if (fc && fc->dev) {
    bool result = false;
    (void)eos_dev_ioctl_call(fc->dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_UMOUNT, &result);
    eos_psram_free(fc);
  }
  return 0;
}

EOS_FS_ATTR eos_fs_type_t eos_fs_fat = {
    .name = "fat",
    .flags = EOS_FS_FLAG_NEEDS_BDEV,
    .mount = eos_fat_mount,
    .umount = eos_fat_umount,
};

EOS_FS_ATTR eos_fs_type_t eos_fs_vfat = {
    .name = "vfat",
    .flags = EOS_FS_FLAG_NEEDS_BDEV,
    .mount = eos_fat_mount,
    .umount = eos_fat_umount,
};
