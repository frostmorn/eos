#include "ecore/bin.h"
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#define MKDIR_MODE 0777

int mkdir_main(int argc, char **argv){
  if (argc < 2) {
    printf("usage: mkdir <path>\n");
    return -1;
  }
  
  const char *path = argv[1];

  if (mkdir(path, MKDIR_MODE)!=0){
    printf("mkdir failed. %d: %s\n", errno, strerror(errno));
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t mkdir_app = {
    EOS_BIN_INITIALIZER, .filename = "mkdir", .name = "mkdir",
    .entry_point = mkdir_main};
