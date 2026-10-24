#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// POSIX exec* declarations
int execl(const char *path, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execv(const char *path, char *const argv[]);
int execve(const char *path, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);

// POSIX wait for child thread execution
int eos_wait(int *status);

// POSIX pipe and file descriptor duplication
int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);

// Explicit alias for EOS system() implementation
int eos_system(const char *cmdline);

// Environment variables (thread-local environ macro and env functions)
char *eos_tctx_getenv(const char *name);
int eos_tctx_setenv(const char *name, const char *value, int overwrite);
int eos_tctx_unsetenv(const char *name);
char **eos_tctx_get_environ(void);

#define environ (eos_tctx_get_environ())
#define getenv(name) eos_tctx_getenv(name)
#define setenv(name, value, overwrite) eos_tctx_setenv(name, value, overwrite)
#define unsetenv(name) eos_tctx_unsetenv(name)

#ifdef __cplusplus
}
#endif

#include_next <unistd.h>
