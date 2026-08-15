#include "appctx.h"
#include <string.h>
#include "emisc/fancymacro.h"
#include "ecore/error.h"

// Root context for an EOS system
static eos_app_ctx_t *eos_root_ctx;

eos_app_ctx_t *eos_get_current_app_ctx(){
  pthread_t cid = pthread_self();

  eos_app_ctx_t *cur = eos_root_ctx;

  do{
    if (cur && cur->tid == cid)
      return cur;
  } while ((cur = cur->kid));

  // stick to root if not found
  return eos_root_ctx;
}


eos_app_ctx_t *eos_app_ctx_alloc(){
  eos_app_ctx_t *cur_ctx = eos_get_current_app_ctx();
  
  // Allocate memory for new context
  eos_app_ctx_t *ctx = malloc(sizeof(eos_app_ctx_t));

  if (!ctx){
    eos_errno = EOS_ERR_NO_MEM_LEFT_ERROR;
    EOS_LOGE("No mem left to allocate application context\n");  
    return NULL;
  }

  memset(ctx, 0, sizeof(eos_app_ctx_t));

  // Copy working dir
  strcpy(ctx->cwd, cur_ctx->cwd);

  // Attach to tree
  ctx->parent = cur_ctx;

  if (cur_ctx->kid){
    ctx->kid = cur_ctx->kid;
    ctx->kid->parent = ctx;
  }
  cur_ctx->kid = ctx;

  return ctx;
}

void eos_app_ctx_free(eos_app_ctx_t *actx){
  if(!actx){
    eos_errno = EOS_ERR_INVALID_ARG;
    abort();
  }
  
  eos_app_ctx_t *parent = actx->parent;
  eos_app_ctx_t *kid = actx->kid;

  if (!parent){
    EOS_LOGE("Context fault\n");
    abort();
  }

  if (kid){
    kid->parent = parent;
  }

  parent->kid = kid; // should be null if null, right

  // free fds

  // free mem
  free(actx);
}

void eos_app_ctx_init(){
  eos_root_ctx = malloc(sizeof(eos_app_ctx_t));
  memset(eos_root_ctx, 0, sizeof(eos_app_ctx_t));

  // Retrieve current thread id
  // eos_root_ctx->tid = pthread_self();

  // Copy default working dir
  strcpy(eos_root_ctx->cwd, EOS_APP_CWD);
   
}
