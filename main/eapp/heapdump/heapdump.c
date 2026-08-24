
#include "ecore/app.h"
#include <esp_heap_caps.h>
#include <stdio.h>

int heapdump_main(int argc, char **argv){
  heap_caps_dump_all();
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t heapdump_app = {
    EOS_NATIVE_APP_INIT, .filename = "heapdump", .name = "heapdump",
    .entry_point = heapdump_main};
