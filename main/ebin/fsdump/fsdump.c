
#include "ecore/bin.h"
#include <stdio.h>
#include <esp_vfs.h>

int fsdump_main(int argc, char **argv){
  esp_vfs_dump_registered_paths(stdout);
  return 0;
}

EOS_BIN_ATTR eos_bin_t fsdump_app = {
    EOS_BIN_INITIALIZER, .filename = "fsdump", .name = "fsdump",
    .entry_point = fsdump_main};
