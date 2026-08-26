#include "ecore/bin.h"
#include <esp_intr_alloc.h>
#include <stdio.h>

int intrdump_main(int argc, char **argv){
  esp_intr_dump(stdout);
  return 0;
}

EOS_BIN_ATTR eos_bin_t intrdump_app = {
    EOS_BIN_INITIALIZER, .filename = "intrdump", .name = "intrdump",
    .entry_point = intrdump_main};
