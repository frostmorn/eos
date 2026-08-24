#include "ecore/app.h"
#include <esp_intr_alloc.h>
#include <stdio.h>

int intrdump_main(int argc, char **argv){
  esp_intr_dump(stdout);
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t intrdump_app = {
    EOS_NATIVE_APP_INIT, .filename = "intrdump", .name = "intrdump",
    .entry_point = intrdump_main};
