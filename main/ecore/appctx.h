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

// Default working directory
#define EOS_APP_CWD "/"

// Limits amount of file descriptors whose can be used
// by an application
#define EOS_APP_FD_MAX 16

// Application context
typedef struct eos_app_ctx_t eos_app_ctx_t;
struct eos_app_ctx_t{
  // Current working directory
  char cwd[PATH_MAX];

  // Current thread id
  pthread_t tid;

  // File descriptors used by process
  int fds[EOS_APP_FD_MAX];
 
  // TODO: memtracking maybe

  // app params
  int argc;
  char **argv;
 
  // Context tree (first child + next sibling - see ecore/tree.h).
  // A parent's contexts can have any number of kids, unlike the
  // single-child chain this used to be.
  EOS_TREE_FIELDS(eos_app_ctx_t);
};

// Generates eos_app_ctx_t_tree_{attach,detach,detach_subtree,walk} -
// see emisc/fancytree.h for what each one does.
EOS_TREE_DECLARE(eos_app_ctx_t);

// Retrieves current application context
eos_app_ctx_t *eos_get_current_app_ctx();

// Allocates new application context
eos_app_ctx_t *eos_app_ctx_alloc();

// Deallocates application context
void eos_app_ctx_free(eos_app_ctx_t *actx);

// Inits root_ctx and prepares system to work with it
void eos_app_ctx_init();


