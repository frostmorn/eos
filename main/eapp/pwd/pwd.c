#include "ecore/app.h"
#include <unistd.h>
#include <stdio.h>

int pwd_main(int argc, char **argv){
  char *path = getcwd(NULL,0); 
  printf("%s\n", path);

  free(path);
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t pwd_app = {
    EOS_NATIVE_APP_INIT, .filename = "pwd", .name = "pwd",
    .entry_point = pwd_main};
