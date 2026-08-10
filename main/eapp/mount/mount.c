#include <stdio.h>
#include <sys/ioctl.h>

#include "ecore/ioctl.h"
#include "ecore/device.h"
#include "ecore/app.h"

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

  // TODO: implement fioctl() helper
  if (!dev_file) {
    printf("Can't open device file. Is it in use?\n");
    return -1;
  }

  int dev_fd = fileno(dev_file);

  eos_dev_t* dev = NULL;
  ioctl(dev_fd, EOS_IOCTL_GET_DEV, &dev);

  if (dev == NULL){
    printf("Can't determine device associated with file %s\n", dev_path); 
    return -1;
  }

  return 0;
  
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t mount_app = {
    EOS_NATIVE_APP_INIT, .filename = "mount", .name = "mount",
    .entry_point = mount_main};
