#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"

//
// DEFINITION: TSS - Thread specific storage
//
// TODO: there's a sense to implement all of this on basis of FreeRTOS TSS
//
// Though, to make it work with any possible threads, we would have to wrap
// all xTaskCreate* from FreeRTOS as well in addition to pthread_create
// since all of those have different thread creation paths
//
// Single common place(prvInitialiseNewTask) which used for any thread 
// creation is not wrapable since defined as static [-_-]==\~
//
// I just only hope we no need to create separate paths to store TSS values,
// but this requires some additional 
//
// RESEARCH: TSS behaviour in pthread and FreeRTOS task(are them same or not)
//
// So for now, and for portatibility, we implement it for pthread specifically
//

static pthread_key_t  tctx_key;
static pthread_once_t tctx_once_init = PTHREAD_ONCE_INIT;

eos_tctx_t* eos_tctx_get(){
  // Ensure init
  eos_tctx_init();
  return pthread_getspecific(tctx_key);
}

eos_tctx_t* eos_tctx_alloc(){
 eos_tctx_t* tctx = malloc(sizeof(eos_tctx_t));

 memset(tctx, 0, sizeof(eos_tctx_t));

 // NOTE: tctx->fds/dirs/memblocks allocated on first access

 if (!tctx)
   EOS_LOGE("Can't allocate new thread context. Not enough memory?\n");

 return tctx;
}

void eos_tctx_free(eos_tctx_t *tctx){
  if (!tctx){
    EOS_LOGW("Trying to free empty thread context\n");
    return;
  }

  EOS_LOGI("Cleaning up thread context\n");
  
  // Active file descriptors:
  if (tctx->fds){
    if (tctx->fds_count){
      EOS_LOGW("Found %d file descriptors in use\n", tctx->fds_count);
      for(size_t i = 0; i < tctx->fds_count; i++){
        close(tctx->fds[i]);
      }
    }
    free(tctx->fds);  
  }
  // Open Directories:
  if (tctx->dirs){
    if (tctx->dirs_count){
      EOS_LOGW("Found %d open directories\n", tctx->dirs_count);
      for(size_t i = 0; i < tctx->dirs_count; i++){
        closedir(tctx->dirs[i]);
      }
    }
    free(tctx->dirs);  
  }
  // Non freed memory blocks
  if (tctx->memblocks){
    if (tctx->memblocks_count){
      EOS_LOGW("Found %d not freed memory blocks\n", tctx->memblocks_count);
      for(size_t i = 0; i < tctx->memblocks_count; i++){
        free(tctx->memblocks[i]);
      }
    }
    free(tctx->memblocks);  
  }

  free(tctx);
}

void eos_tctx_reg_fd(int fd, eos_tctx_t *tctx){
  // Check context
  if (!tctx){
    EOS_LOGW("Trying to register file descriptor for empty thread context\n");
    return;
  }
  // Check fd
  if (fd < 0) {
    EOS_LOGE("Trying to register bad fd %d within thread context\n", fd);
    return;
  }

  // Check if memory allocated for fd storage
  if (!tctx->fds){
    tctx->fds = malloc(sizeof(int) * 2);
    if (!tctx->fds){
      EOS_LOGE("Memory allocation for thread context fds failed\n");
      return;
    }
    tctx->fds_count = 0;
    tctx->fds_cap = 2;
    // Fill with bad fds
    tctx->fds[0] = -1;
    tctx->fds[1] = -1;
  }

  // Allocate additional memory if needed
  if(tctx->fds_cap == tctx->fds_count){
    size_t newcap = tctx->fds_cap * 2;
    // RESERARCH: how reallocarray() would work with PSRAM blocks
    void * newblock = reallocarray(tctx->fds, newcap, sizeof(int));
    if (!newblock){
      EOS_LOGE("Memory allocation failed for thread context fds storage\n");
      return;
    }
    // Swap blocks
    tctx->fds = newblock;
    tctx->fds_count = newcap; 

    // Fill with bad fds
    for (size_t i = tctx->fds_count; i < newcap; i++){
      tctx->fds[i] = -1;
    }
  }

  // Register
  tctx->fds[tctx->fds_count] = fd;
  tctx->fds_count++;
}

void eos_tctx_unreg_fd(int fd, eos_tctx_t *tctx){
  // Check context
  if (!tctx){
    EOS_LOGW("Trying to unregister file descriptor for empty thread context\n");
    return;
  }
  // Check fd
  if (fd < 0) {
    EOS_LOGE("Trying to unregister bad fd %d within thread context\n", fd);
    return;
  }

  // Find fd slot
  int *pfd = NULL;
  size_t i;
  if ((tctx->fds && tctx->fds_count)){
    for (i = 0; i < tctx->fds_count; i++){
      if (tctx->fds[i] == fd){
        pfd = &(tctx->fds[i]);
        break;
      }
    }
  } // oh my god

  // Check if fd found
  if (!pfd){
    EOS_LOGE("fd %d not found. impossible to unregister\n", fd);
    return;
  }

  // Handle special case
  if (tctx->fds_count == 1){
    tctx->fds_count = 0;
    tctx->fds_cap = 0;
    free(tctx->fds);
    tctx->fds = NULL;
    return;
  }

  // Unregister and compact list
  tctx->fds_count--;
  tctx->fds[i] = tctx->fds[tctx->fds_count];
  tctx->fds[tctx->fds_count] = -1;

  // Shrink mem if needed
  if (tctx->fds_count * 3 < tctx->fds_cap)
  {
    size_t newcap = tctx->fds_cap * 2;
    // Probably we can use just realloc here, but well, who cares
    void * newblock = reallocarray(tctx->fds, newcap, sizeof(int));
    if (!newblock){
      EOS_LOGE("Memory allocation failed for thread context fds storage\n");
      return;
    }
    tctx->fds = newblock;
  } 
}

// Almost exact copy of tctx_reg_fd
// if do changes here, apply them in both places
void eos_tctx_reg_dir(DIR *dir, eos_tctx_t *tctx){
  // Check context
  if (!tctx){
    EOS_LOGW("Trying to register dir for empty thread context\n");
    return;
  }
  // Check dir
  if (!dir) {
    EOS_LOGE("Trying to register dir %p within thread context\n", dir);
    return;
  }

  // Check if memory allocated for dir storage
  if (!tctx->dirs){
    tctx->dirs = malloc(sizeof(int) * 2);
    if (!tctx->dirs){
      EOS_LOGE("Memory allocation for thread context dirs failed\n");
      return;
    }
    tctx->dirs_count = 0;
    tctx->dirs_cap = 2;
    // FIll with bad dirs
    tctx->dirs[0] =  NULL;
    tctx->dirs[1] = NULL;
  }

  // Allocate additional memory if needed
  if(tctx->dirs_cap == tctx->dirs_count){
    size_t newcap = tctx->dirs_cap * 2;
    // RESERARCH: how reallocarray() would work with PSRAM blocks
    void * newblock = reallocarray(tctx->dirs, newcap, sizeof(DIR*));
    if (!newblock){
      EOS_LOGE("Memory allocation failed for thread context dirs storage\n");
      return;
    }
    // Swap blocks
    tctx->dirs = newblock;
    tctx->dirs_count = newcap; 

    // Fill with bad dirs
    for (size_t i = tctx->dirs_count; i < newcap; i++){
      tctx->dirs[i] = NULL;
    }
  }

  // Register
  tctx->dirs[tctx->dirs_count] = dir;
  tctx->dirs_count++;

}

// Almost exact copy of tctx_unreg_fd
// if do changes here, apply them in both places
void eos_tctx_unreg_dir(DIR *dir, eos_tctx_t *tctx){
  // Check context
  if (!tctx){
    EOS_LOGW("Trying to unregister dir for empty thread context\n");
    return;
  }
  // Check dir
  if (dir) {
    EOS_LOGE("Trying to unregister bad dir %p within thread context\n", dir);
    return;
  }

  // Find dir slot
  DIR **ppdir = NULL;
  size_t i;
  if ((tctx->dirs && tctx->dirs_count)){
    for (i = 0; i < tctx->dirs_count; i++){
      if (tctx->dirs[i] == dir){
        ppdir = &(tctx->dirs[i]);
        break;
      }
    }
  } // oh my god

  // Check if dir found
  if (!ppdir){
    EOS_LOGE("dir %p not found. impossible to unregister\n", dir);
    return;
  }

  // Handle special case
  if (tctx->dirs_count == 1){
    tctx->dirs_count = 0;
    tctx->dirs_cap = 0;
    free(tctx->dirs);
    tctx->dirs = NULL;
    return;
  }

  // Unregister and compact list
  tctx->dirs_count--;
  tctx->dirs[i] = tctx->dirs[tctx->dirs_count];
  tctx->dirs[tctx->dirs_count] = NULL;

  // Shrink mem if needed
  if (tctx->dirs_count * 3 < tctx->dirs_cap)
  {
    size_t newcap = tctx->dirs_cap * 2;
    // Probably we can use just realloc here, but well, who cares
    void * newblock = reallocarray(tctx->dirs, newcap, sizeof(DIR*));
    if (!newblock){
      EOS_LOGE("Memory allocation failed for thread context dir storage\n");
      return;
    }
    tctx->dirs = newblock;
  } 

}

void eos_tctx_reg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx){

}

void eos_tctx_unreg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx){

}

static void tctx_key_init(){
  pthread_key_create(&tctx_key, (void (*)(void *))eos_tctx_free);
  eos_tctx_t *tctx = eos_tctx_alloc();
  strcpy(tctx->cwd, "/"); // root dir
}

void eos_tctx_init(){
  pthread_once(&tctx_once_init, tctx_key_init);
}
