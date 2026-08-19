#include "appctx.h"
#include <string.h>
#include "emisc/fancymacro.h"
#include "ecore/error.h"

// Generates eos_app_ctx_t_tree_attach/detach/detach_subtree/walk,
// operating on this struct's parent/child/next fields (see
// ecore/tree.h - the same generated logic backs the device tree).
EOS_TREE_DEFINE(eos_app_ctx_t);

// Root context for an EOS system
static eos_app_ctx_t *eos_root_ctx = NULL;

typedef struct {
  pthread_t target;
  eos_app_ctx_t *found;
} eos_app_ctx_lookup_t;

static bool eos_app_ctx_lookup_visit(eos_app_ctx_t *ctx, void *vctx){
  eos_app_ctx_lookup_t *lookup = (eos_app_ctx_lookup_t *)vctx;
  if (pthread_equal(ctx->tid, lookup->target)){
    lookup->found = ctx;
    return false; // stop walking, we're done
  }
  return true;
}

eos_app_ctx_t *eos_app_ctx_get_cur(){
  // Nothing initialized yet
  if (!eos_root_ctx) return NULL;

  eos_app_ctx_lookup_t lookup = { .target = pthread_self(), .found = NULL };

  eos_app_ctx_t_tree_walk(eos_root_ctx, eos_app_ctx_lookup_visit, &lookup);

  // stick to root if not found
  return lookup.found ? lookup.found : eos_root_ctx;
}

eos_app_ctx_t *eos_app_ctx_alloc(){
  eos_app_ctx_t *cur_ctx = eos_app_ctx_get_cur();
 
  EOS_LOGI("Allocating new application context\n");  
  
  // Allocate memory for new context
  eos_app_ctx_t *ctx = malloc(sizeof(eos_app_ctx_t));

  if (!ctx){
    eos_errno = EOS_ERR_NO_MEM_LEFT_ERROR;
    EOS_LOGE("No mem left to allocate application context\n");  
    return NULL;
  }

  memset(ctx, 0, sizeof(eos_app_ctx_t));

  // fill with invalid fds
  for (size_t i = 0; i < EOS_APP_FD_MAX; i++){
    ctx->fds[i] = -1; // invalid fd
  }  

  // Copy working dir
  strcpy(ctx->cwd, cur_ctx->cwd);

  // Attach to the context tree as cur_ctx's newest kid - a sibling of
  // any kids cur_ctx already has, not their new parent.
  eos_app_ctx_t_tree_attach(ctx, cur_ctx);

  return ctx;
}

void eos_app_ctx_free(eos_app_ctx_t *actx){

  EOS_LOGI("Application context is about to be freed\n");  

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
  for (size_t i = 0; i < EOS_APP_FD_MAX; i++){
    if (actx->fds[i] != -1)
    {
      EOS_LOGW("File descriptor %d leaked\n", actx->fds[i]);
      close(actx->fds[i]);
      actx->fds[i] = -1;
    }
  }

  // free dirs
  for (size_t i = 0; i < EOS_APP_DIR_MAX; i++){
    if (actx->dirs[i] != NULL)
    {
      EOS_LOGW("Directory %p leaked\n", actx->dirs[i]);
      closedir(actx->dirs[i]);
      actx->dirs[i] = NULL;
    }
  }

  // free mem
  free(actx);
}

void eos_app_ctx_init(){
  eos_root_ctx = malloc(sizeof(eos_app_ctx_t));
  memset(eos_root_ctx, 0, sizeof(eos_app_ctx_t));

  // fill with invalid fds
  for (size_t i = 0; i < EOS_APP_FD_MAX; i++){
    eos_root_ctx->fds[i] = -1; // invalid fd
  }  

  // Retrieve current thread id
  // eos_root_ctx->tid = pthread_self();

  // Copy default working dir
  strcpy(eos_root_ctx->cwd, EOS_APP_CWD);
   
}

// TODO: do I've to care about ctx NULL reference?

bool eos_app_ctx_reg_fd(int fd, eos_app_ctx_t *ctx){
  if (ctx == NULL){
    EOS_LOGW("fd %d outside of context\n", fd);
    return true; // Can happen on init
  }
  // find fd slot
  for (size_t i = 0; i < EOS_APP_FD_MAX; i++){
    if (ctx->fds[i] == -1){
      ctx->fds[i] = fd;
      return true;
    }
  }
  // No slots? 
  EOS_LOGE("No slot left for fd %d register", fd);
  return false;
}

void eos_app_ctx_unreg_fd(int fd, eos_app_ctx_t *ctx){
  if (ctx == NULL){
    EOS_LOGW("fd %d outside of context\n", fd);
    return; // Can happen on init
  }
  for (size_t i=0; i < EOS_APP_FD_MAX; i++){
    if (fd == ctx->fds[i]){
      ctx->fds[i] = -1;
      return;
    }
  }
  // That's veri veri badie badie
  EOS_LOGE("Tried to unreg not registered fd %d", fd);
}

// DIR management
bool eos_app_ctx_reg_dir(DIR *dir, eos_app_ctx_t *ctx){
  if (ctx == NULL){
    EOS_LOGW("dir %p outside of context\n", dir);
    return true; // Can happen on init
  }
  // find dir slot
  for (size_t i = 0; i < EOS_APP_DIR_MAX; i++){
    if (ctx->dirs[i] == NULL){
      ctx->dirs[i] = dir;
      return true;
    }
  }
  // No slots? 
  EOS_LOGE("No slot left for dir %p register", dir);
  return false;
}

void eos_app_ctx_unreg_dir(DIR *dir, eos_app_ctx_t *ctx){
  if (ctx == NULL){
    EOS_LOGW("dir %p outside of context\n", dir);
    return; // Can happen on init
  }
  for (size_t i=0; i < EOS_APP_DIR_MAX; i++){
    if (dir == ctx->dirs[i]){
      ctx->dirs[i] = NULL;
      return;
    }
  }
  // That's veri veri badie badie
  EOS_LOGE("Tried to unreg not registered dir %p", dir);
}

// Memory management
bool eos_app_ctx_reg_memblock(void *block, size_t blocksize, eos_app_ctx_t *ctx){
  return true; // TODO: add logic here
}

void eos_app_ctx_unreg_memblock(void *block, size_t blocksize, eos_app_ctx_t *ctx){

}
