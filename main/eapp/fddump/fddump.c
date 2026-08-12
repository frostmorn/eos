#include "ecore/app.h"
#include <stdio.h>
#include <esp_vfs.h>

int fddump_main(int argc, char **argv){
  esp_vfs_dump_fds(stdout);
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t fddump_app = {
    EOS_NATIVE_APP_INIT, .filename = "fddump", .name = "fddump",
    .entry_point = fddump_main};
