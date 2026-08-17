#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// (^__^)==\~ /////////////////////////////////////////////////////
//
// Since esp-idf allows us to wrap almost everything 
// via linker magic(check main/CMakeLists.txt)
// it is possible to wrap around all our fancy app
// in a sugarfull environment, allowing theoretically to
//
// 1. Track current working directory
// 2. Perform much better path resolution
// 3. Track used file descriptors
// 4. Memory! We can see memory!
//
// we can do everything, except acessing something static [-_-]==|-
//
///////////////////////////////////////////////////////////////////

#include <limits.h> // PATH_MAX
#include <pthread.h>
#include "emisc/fancytree.h"
#include "ecore/app.h"
#include <stdio.h>
#include <dirent.h>

// Default working directory
#define EOS_APP_CWD "/"

// Limits amount of file descriptors whose can be used
// by an application
// TODO: introduce it as a quota provided in manifest
#define EOS_APP_FD_MAX 16
// In esp-idf, DIR doesn't seem to have a fd
#define EOS_APP_DIR_MAX 16

/* TODO: dynamic fds table allocation
#define EOS_APP_FD_PER_BLOCK 16


// Block of open file descriptors
typedef struct eos_app_fd_block_t eos_app_fd_block_t;
struct eos_app_fd_block_t{
  int fds[EOS_APP_FD_PER_BLOCK];
  eos_app_fd_block_t *next;
};
*/

// Application context
typedef struct eos_app_ctx_t eos_app_ctx_t;
struct eos_app_ctx_t{
// Runtime data:
  // Current working directory
  char cwd[PATH_MAX];

  // Current thread id
  pthread_t tid;

  // File descriptors used by process
  int fds[EOS_APP_FD_MAX];
  DIR *dirs[EOS_APP_DIR_MAX];
  // TODO: memtracking maybe

// Static data
  int argc;
  char **argv; 
  eos_native_app_manifest_t *manifest;

// Tree data:
  EOS_TREE_FIELDS(eos_app_ctx_t);
};

// Generates eos_app_ctx_t_tree_{attach,detach,detach_subtree,walk} -
// see emisc/fancytree.h for what each one does.
EOS_TREE_DECLARE(eos_app_ctx_t);

// Retrieves current application context
eos_app_ctx_t *eos_app_ctx_get_cur();

// Allocates new application context
eos_app_ctx_t *eos_app_ctx_alloc();

// Deallocates application context
// Closes all opened files and dirs within that application context
void eos_app_ctx_free(eos_app_ctx_t *actx);

// Inits root_ctx and prepares system to work with it
void eos_app_ctx_init();

// FD management
bool eos_app_ctx_reg_fd(int fd, eos_app_ctx_t *ctx);
void eos_app_ctx_unreg_fd(int fd, eos_app_ctx_t *ctx);

// DIR management
bool eos_app_ctx_reg_dir(DIR *dir, eos_app_ctx_t *ctx);
void eos_app_ctx_unreg_dir(DIR *dir, eos_app_ctx_t *ctx);

// Memory management
bool eos_app_ctx_reg_memblock(void *block, size_t blocksize, eos_app_ctx_t *ctx);
void eos_app_ctx_unreg_memblock(void *block, size_t blocksize, eos_app_ctx_t *ctx);
