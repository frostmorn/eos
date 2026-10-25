#include "ecore/bin.h"
#include <stdio.h>
#include <string.h>
int fdsetcheck_main(int argc, char **argv){
  // Entry point of your application

  printf("FD set set to %d\n", FD_SETSIZE);
  
  const char filename[64];

  size_t index = 0;
  while(1){
    sprintf(filename, "/%d.fdsetcheck", index);
    FILE *ftry = fopen(filename, "w+");

    if (!ftry) break;

    index++;
    // TODO: System should terminate files by own (exec/execve/etc.)
  }

  printf("Succesfully opened %d files\n", index);
  return 0;
}

///////////////////////////////////////////////////////////////////////
// BIN MANIFEST:
///////////////////////////////////////////////////////////////////////
EOS_BIN_ATTR eos_bin_t fdsetcheck_app = {
  EOS_BIN_INITIALIZER,
  .flags       = 0,
  .filename    = "fdsetcheck",
  .name        = "FD Set Check",
  .group       = "Diag",
  .description = "Test maximum open file descriptors against FD_SETSIZE limit",
  .entry_point = fdsetcheck_main
};
///////////////////////////////////////////////////////////////////////

// Generated on 2026-10-09 09:10:19
// GIT HEAD at f1827b2 

// (^__^)==\~
