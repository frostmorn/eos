#include "ecore/bin.h"
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int rmdir_main(int argc, char **argv){
  if (argc !=2){
    printf("Usage: rmdir <folder_name>\n");
    return -1;
  }
 
  const char *path = argv[1];
 
  if (rmdir(path) != 0){
    printf("rmdir: %s: %d:%s\n", path, errno, strerror(errno));
  }  
  
  return 0;
}

///////////////////////////////////////////////////////////////////////
// BIN MANIFEST:
///////////////////////////////////////////////////////////////////////
EOS_BIN_ATTR eos_bin_t rmdir_app = {
  EOS_BIN_INITIALIZER,
  .flags       = 0,
  .filename    = "rmdir",
  .name        = "rmdir",
  .group       = "Utils",
  .description = "Remove directories",
  .entry_point = rmdir_main
};
///////////////////////////////////////////////////////////////////////

// Generated on 2026-09-16 01:16:06
// GIT HEAD at d9228cd 

// (^__^)==\~
