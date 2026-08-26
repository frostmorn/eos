#include "ecore/bin.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int cd_main(int argc, char **argv){
  const char *path = (argc < 2) ? "/" : argv[1];

  if (chdir(path) !=0){
    printf("cd: %s: %d:%s", path, errno, strerror(errno));
    return -1;
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t cd_app = {
    EOS_BIN_INITIALIZER, .filename = "cd", .name = "cd",
    .entry_point = cd_main};
