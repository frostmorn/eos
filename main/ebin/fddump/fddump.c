#include "ecore/bin.h"
#include <stdio.h>
#include <esp_vfs.h>

int fddump_main(int argc, char **argv){
  esp_vfs_dump_fds(stdout);
  return 0;
}

EOS_BIN_ATTR eos_bin_t fddump_app = {
    EOS_BIN_INITIALIZER, .filename = "fddump", .name = "fddump",
    .entry_point = fddump_main};
