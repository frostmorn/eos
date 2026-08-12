
#include "ecore/app.h"
#include <stdio.h>
#include <esp_vfs.h>

int fsdump_main(int argc, char **argv){
  esp_vfs_dump_registered_paths(stdout);
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t fsdump_app = {
    EOS_NATIVE_APP_INIT, .filename = "fsdump", .name = "fsdump",
    .entry_point = fsdump_main};
