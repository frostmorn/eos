#include "appctx.h"
#include <string.h>

// Root context for an EOS system
static eos_app_ctx_t *eos_root_ctx;


void eos_app_ctx_init(){
  eos_root_ctx = malloc(sizeof(eos_app_ctx_t));
  memset(eos_root_ctx, 0, sizeof(eos_app_ctx_t));

  // Retrieve current thread id
  eos_root_ctx->tid = pthread_self();

  // Copy default working dir
  strcpy(eos_root_ctx->cwd, EOS_APP_CWD);

   
}
