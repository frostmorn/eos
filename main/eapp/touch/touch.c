#include <stdio.h>
#include "ecore/app.h"
#include <errno.h>
#include <string.h>

int touch_main(int argc, char **argv){
  if (argc < 2){ 
    printf("Usage: touch <filename>\n");
    return -1;
  }
  
  const char* path = argv[1];

  FILE *fp = fopen(path, "a");

  if (fp){
    fclose(fp);
    return 0;
  }

  fp = fopen(path, "w");

  if (!fp){
    printf("Can't create file %s. %d: %s\n", path, errno, strerror(errno));
  }
  
  fclose(fp);

  return 0;
}


EOS_NATIVE_APP_ATTR eos_native_app_manifest_t touch_app = {
    EOS_NATIVE_APP_INIT, .filename = "touch", .name = "touch",
    .entry_point = touch_main};
