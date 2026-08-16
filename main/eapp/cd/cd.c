#include "ecore/app.h"
#include "ecore/unistd_ext.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>

int cd_main(int argc, char **argv){
  const char *path = (argc < 2) ? "/" : argv[1];

  if (chdir(path) !=0){
    printf("cd: %s: %d:%s", path, errno, strerror(errno));
    return -1;
  }

  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t cd_app = {
    EOS_NATIVE_APP_INIT, .filename = "cd", .name = "cd",
    .entry_point = cd_main};
