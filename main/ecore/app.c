#include "app.h"
#include "emisc/fancymacro.h"

int eos_native_app_entry_point_empty(int argc, char **argv) {
  EOS_LOGW("Call to not implemented entry_point for an application. ARGC = "
           "%d\nARGV:",
           argc);
  for (int i = 0; i < argc; i++) {
    EOS_LOGW("[%d]%s", i, argv[i]);
  }

  return -1;
}
