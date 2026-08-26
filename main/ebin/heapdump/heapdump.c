
#include "ecore/bin.h"
#include <esp_heap_caps.h>
#include <stdio.h>

int heapdump_main(int argc, char **argv){
  heap_caps_dump_all();
  return 0;
}

EOS_BIN_ATTR eos_bin_t heapdump_app = {
    EOS_BIN_INITIALIZER, .filename = "heapdump", .name = "heapdump",
    .entry_point = heapdump_main};
