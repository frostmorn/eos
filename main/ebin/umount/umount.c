#include <stdio.h>
#include <sys/ioctl.h>

#include "ecore/ioctl.h"
#include "ecore/dev.h"
#include "ecore/bin.h"

int umount_main(int argc, char **argv){
  if (argc < 2){
    printf("umount Usage: umount <file>\n");
    return -1;
  }

  // TODO: allow unmount by vfs path as well 
  const char *dev_path = argv[1];
  printf("Trying to unmount %s\n", dev_path);

  // Actual unmounting would be done by  EOS
  FILE *dev_file = fopen(dev_path, "r");

  if (!dev_file) {
    printf("Can't open device file. Is it in use?\n");
    return -1;
  }

  int dev_fd = fileno(dev_file);

  bool umount_result;
  ioctl(dev_fd, EOS_STORAGE_IOCTL_UMOUNT, &umount_result);

  if (!umount_result){
    printf("unmount failed\n");
  }
  
  fclose(dev_file); 

  return 0;
  
}

EOS_BIN_ATTR eos_bin_t umount_app = {
    EOS_BIN_INITIALIZER, .filename = "umount", .name = "umount",
    .entry_point = umount_main};
