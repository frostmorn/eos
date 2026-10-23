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

#ifdef __cplusplus
}
#endif

#include_next <unistd.h>
