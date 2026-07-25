#include "binfs.h"

void eos_binfs_init() {
  // for (uint32_t i = 0; i < EOS_MAX_DEVICES; i++)
  //   if (eos_devices[i].in_use)
  //     eos_devices[i].fd = -1;

  // static const esp_vfs_t vfs = {
  //     .flags = ESP_VFS_FLAG_CONTEXT_PTR,
  //     .open_p = binfs_open,
  //     .close_p = binfs_close,
  //     .read_p = binfs_read,
  //     .write_p = binfs_write,
  //     .ioctl_p = binfs_ioctl,
  //     .lseek_p = binfs_lseek,
  //     .opendir_p = binfs_opendir,
  //     .readdir_p = binfs_readdir,
  //     .seekdir_p = binfs_seekdir,
  //     .telldir_p = binfs_telldir,
  //     .closedir_p = binfs_closedir,
  // };

  // esp_vfs_register(EOS_BINFS_ROOT, &vfs, NULL);
  EOS_LOGI("binfs mounted at %s", EOS_BINFS_ROOT);
}