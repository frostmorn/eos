#include <stdio.h>
#include <sys/ioctl.h>

#include "ecore/ioctl.h"
#include "ecore/device.h"
#include "ecore/bin.h"

int mount_main(int argc, char **argv){
  if (argc < 3){
    printf("mount Usage: mount <file> <path>\n");
    return -1;
  }
 
  const char *dev_path = argv[1];
  const char *mount_path = argv[2];
 
  printf("Trying to mount %s to %s\n", dev_path, mount_path);

  // Actual mounting would be done by  EOS
  FILE *dev_file = fopen(dev_path, "r");

  if (!dev_file) {
    printf("Can't open device file. Is it in use?\n");
    return -1;
  }

  int dev_fd = fileno(dev_file);

  bool mount_result;
  ioctl(dev_fd, EOS_STORAGE_IOCTL_MOUNT, mount_path, &mount_result);

  if (!mount_result){
    printf("Mount failed\n");
  }
  
  fclose(dev_file); 

  return 0;
  
}

EOS_BIN_ATTR eos_bin_t mount_app = {
    EOS_BIN_INITIALIZER, .filename = "mount", .name = "mount",
    .entry_point = mount_main};
