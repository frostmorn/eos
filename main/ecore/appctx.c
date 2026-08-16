#include "appctx.h"
#include <string.h>
#include "emisc/fancymacro.h"
#include "ecore/error.h"

// Generates eos_app_ctx_t_tree_attach/detach/detach_subtree/walk,
// operating on this struct's parent/child/next fields (see
// ecore/tree.h - the same generated logic backs the device tree).
EOS_TREE_DEFINE(eos_app_ctx_t);

// Root context for an EOS system
static eos_app_ctx_t *eos_root_ctx;

typedef struct {
  pthread_t target;
  eos_app_ctx_t *found;
} eos_app_ctx_lookup_t;

static bool eos_app_ctx_lookup_visit(eos_app_ctx_t *ctx, void *vctx){
  eos_app_ctx_lookup_t *lookup = (eos_app_ctx_lookup_t *)vctx;
  if (ctx->tid == lookup->target){
    lookup->found = ctx;
    return false; // stop walking, we're done
  }
  return true;
}

eos_app_ctx_t *eos_get_current_app_ctx(){
  eos_app_ctx_lookup_t lookup = { .target = pthread_self(), .found = NULL };

  eos_app_ctx_t_tree_walk(eos_root_ctx, eos_app_ctx_lookup_visit, &lookup);

  // stick to root if not found
  return lookup.found ? lookup.found : eos_root_ctx;
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

  // Attach to the context tree as cur_ctx's newest kid - a sibling of
  // any kids cur_ctx already has, not their new parent.
  eos_app_ctx_t_tree_attach(ctx, cur_ctx);

  return ctx;
}

void eos_app_ctx_free(eos_app_ctx_t *actx){
  if(!actx){
    eos_errno = EOS_ERR_INVALID_ARG;
    abort();
  }

  if (!actx->parent){
    EOS_LOGE("Context fault\n");
    abort();
  }

  // Detaches actx from the tree. Its own kids (if any) are reparented
  // onto actx's former parent rather than destroyed with it - an app
  // exiting doesn't take its still-running children down with it.
  eos_app_ctx_t_tree_detach(actx);

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
