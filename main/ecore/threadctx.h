#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
//
// TODO: This part is a redesign for existing application context
//
///////////////////////////////////////////////////////

#include <pthread.h>
#include <limits.h>
#include <dirent.h>

typedef struct eos_tctx_t eos_tctx_t;
struct eos_tctx_t{
  char   cwd  [PATH_MAX];         // Inherits from current thread
  // Those pieces are almost identical, maybe use kvec from klib?
  struct{
    int *fds;
    size_t fds_count;
    size_t fds_cap;
  };
  struct{
    DIR **dirs;
    size_t dirs_count;
    size_t dirs_cap;
  };
  struct{
    // TODO: store other meta?
    void **memblocks;
    size_t memblocks_count;
    size_t memblocks_cap;
  };
};

// Retrieves current thread context
eos_tctx_t* eos_tctx_get();

// Allocates new thread context
eos_tctx_t* eos_tctx_alloc();

// Dealocates thread context 
void eos_tctx_free(eos_tctx_t *tctx);

// FD management
void eos_tctx_reg_fd(int fd, eos_tctx_t *tctx);
void eos_tctx_unreg_fd(int fd, eos_tctx_t *tctx);

// DIR management
void eos_tctx_reg_dir(DIR *dir, eos_tctx_t *tctx);
void eos_tctx_unreg_dir(DIR *dir, eos_tctx_t *tctx);

// Memory management
void eos_tctx_reg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx);
void eos_tctx_unreg_memblock(void *block, size_t blocksize, eos_tctx_t *tctx);

// Global thread context init
void eos_tctx_init();
