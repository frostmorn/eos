#include "ecore/bin.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int pwd_main(int argc, char **argv){
  char *path = getcwd(NULL,0); 
  printf("%s\n", path);

  free(path);
  return 0;
}

EOS_BIN_ATTR eos_bin_t pwd_app = {
    EOS_BIN_INITIALIZER, .filename = "pwd", .name = "pwd",
    .entry_point = pwd_main};
