#include "ecore/bin.h"
#include <stdio.h>
#include <errno.h>

int write_main(int argc, char **argv){
  
  if (argc != 3){
    printf("Usage: write <value> <file>\n"); 
  } 

  FILE *f = fopen(argv[2], "w");
  if (!f){
    printf("write: %d:%s\n", errno, strerror(errno));
    return -1;
  }

  fwrite(argv[1], 1,  strlen(argv[1]), f);

  fclose(f);

  return 0;
}

EOS_BIN_ATTR eos_bin_t write_app = {
  EOS_BIN_INITIALIZER, 
  .filename    = "write",
  .name        = "write",
  .entry_point = write_main
};
