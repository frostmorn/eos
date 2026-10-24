#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
//
// (^__^)==\~ pipefs - in-memory ring-buffer pipe VFS
//
// Exposes anonymous pipes (pipe(int pipefd[2])) and named
// pipe nodes under /pipe/[id] via ESP-IDF's VFS layer.
//

#include "emisc/strlimits.h"
#include <stddef.h>

#ifndef EOS_PIPEFS_ROOT
#define EOS_PIPEFS_ROOT "/pipe"
#endif

#ifndef EOS_PIPE_BUF_SIZE
#define EOS_PIPE_BUF_SIZE 1024
#endif

#ifndef EOS_PIPEFS_MAX_PIPES
#define EOS_PIPEFS_MAX_PIPES 16
#endif

#ifndef EOS_PIPEFS_MAX_FDS
#define EOS_PIPEFS_MAX_FDS 32
#endif

// Initializes and mounts pipefs at EOS_PIPEFS_ROOT
void eos_pipefs_init(void);

// Creates a new pipe instance in pipefs and writes its VFS path
// (e.g. "/pipe/p0") into path_out. Returns pipe slot index on success, -1 on error.
int eos_pipefs_create(char *path_out, size_t len);

// POSIX pipe() - allocates a pipe and opens read/write VFS file descriptors
int pipe(int pipefd[2]);
