#include "efs/fat/fat.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include <string.h>

int eos_fat_mount(const char *source, const char *target, uint32_t flags, const char *opts, void **out_ctx) {
  (void)flags; (void)opts;
  if (!source) return -1;
  const char *dev_name = source;
  if (strncmp(dev_name, "/dev/", 5) == 0) dev_name += 5;
  eos_dev_t *dev = eos_dev_find_by_name(dev_name);
  if (!dev) return -1;

  bool result = false;
  int rc = eos_dev_ioctl_call(dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_MOUNT, target, &result);
  if (rc != 0 || !result) return -1;
  if (out_ctx) *out_ctx = dev;
  return 0;
}

int eos_fat_umount(const char *target, void *ctx, uint32_t flags) {
  (void)target; (void)flags;
  eos_dev_t *dev = (eos_dev_t *)ctx;
  if (!dev) return -1;
  bool result = false;
  int rc = eos_dev_ioctl_call(dev, EOS_DRV_FD, EOS_STORAGE_IOCTL_UMOUNT, &result);
  return (rc == 0 && result) ? 0 : -1;
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
