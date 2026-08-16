#include "ecore/app.h"
#include "ecore/unistd_ext.h"

int pwd_main(int argc, char **argv){
  // getcwd_fast never fails
  printf("%s\n", getcwd_fast());
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t tree_app = {
    EOS_NATIVE_APP_INIT, .filename = "pwd", .name = "pwd",
    .entry_point = pwd_main};
