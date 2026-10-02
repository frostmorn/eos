#include "ecore/bin.h"
#include <stdio.h>
//
//
//
int argvdump_main(int argc, char ** argv){
  printf("argvdump: %d cmd params found\n", argc);

  for (size_t i = 0; i < argc; i++){
    printf("argv[%d] %s\n", i, argv[i]);
  }

  printf("\n");

  return 0;
}

EOS_BIN_ATTR eos_bin_t argvdump_app = {
    EOS_BIN_INITIALIZER, .filename = "argvdump", .name = "argvdump",
    .entry_point = argvdump_main};
