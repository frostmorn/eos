#include "ecore/bin.h"

int $BIN_NAME$_main(int argc, char **argv){
  // Entry point of your application
  return 0;
}

///////////////////////////////////////////////////////////////////////
// BIN MANIFEST:
///////////////////////////////////////////////////////////////////////
EOS_BIN_ATTR eos_bin_t $BIN_NAME$_app = {
  EOS_BIN_INITIALIZER,
  .flags       = $BIN_FLAGS$,
  .filename    = "$BIN_NAME$",
  .name        = "$BIN_UI_NAME$",
  .group       = "$BIN_GROUP$",
  .description = "$BIN_DESC$",
  .entry_point = $BIN_NAME$_main
};
///////////////////////////////////////////////////////////////////////

// Generated on $_DATE$
// GIT HEAD at $_COMMIT$ 

// (^__^)==\~
