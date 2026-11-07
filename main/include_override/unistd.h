#pragma once

#include <sys/types.h>

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

// POSIX process management declarations
pid_t eos_getpid(void);
pid_t eos_getppid(void);
pid_t eos_getpgid(pid_t pid);
int eos_setpgid(pid_t pid, pid_t pgid);
pid_t eos_setsid(void);
pid_t eos_waitpid(pid_t pid, int *status, int options);

// POSIX pipe and file descriptor duplication
int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);

// Explicit alias for EOS system() implementation
int eos_system(const char *cmdline);

// Environment variables (process-level environ macro and env functions)
char *eos_proc_getenv(const char *name);
int eos_proc_setenv(const char *name, const char *value, int overwrite);
int eos_proc_unsetenv(const char *name);
char **eos_proc_get_environ(void);

#define environ (eos_proc_get_environ())
#define getenv(name) eos_proc_getenv(name)
#define setenv(name, value, overwrite) eos_proc_setenv(name, value, overwrite)
#define unsetenv(name) eos_proc_unsetenv(name)

#ifdef __cplusplus
}
#endif

#include_next <unistd.h>
