#include <stdio.h>
#include "ecore/bin.h"
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


EOS_BIN_ATTR eos_bin_t touch_app = {
    EOS_BIN_INITIALIZER, .filename = "touch", .name = "touch",
    .entry_point = touch_main};
