#include "ecore/bin.h"
#include "ecore/proc.h"
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  int passed;
  int failed;
} test_ctx_t;

static void report_test(test_ctx_t *ctx, const char *name, bool ok) {
  if (ok) {
    printf("  [PASS] %s\n", name);
    ctx->passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    ctx->failed++;
  }
}

static void test_posix_getters_and_setters(test_ctx_t *ctx) {
  pid_t my_pid = getpid();
  report_test(ctx, "proc: getpid() returns valid positive PID", my_pid > 0);

  pid_t my_ppid = getppid();
  report_test(ctx, "proc: getppid() returns valid non-negative PPID", my_ppid >= 0);

  pid_t my_pgid = getpgid(0);
  report_test(ctx, "proc: getpgid(0) returns valid positive PGID", my_pgid > 0);

  int setpgid_rc = setpgid(0, 0);
  report_test(ctx, "proc: setpgid(0, 0) returns 0", setpgid_rc == 0);
  report_test(ctx, "proc: getpgid(0) after setpgid(0, 0) equals current PID", getpgid(0) == my_pid);

  pid_t new_sid = setsid();
  report_test(ctx, "proc: setsid() returns valid positive SID", new_sid > 0);
  report_test(ctx, "proc: setsid() sets session ID equal to current PID", new_sid == my_pid);

  report_test(ctx, "proc: getpgid(99999) on non-existent process sets ESRCH",
              getpgid(99999) == -1 && errno == ESRCH);
  report_test(ctx, "proc: setpgid(99999, 1) on non-existent process sets ESRCH",
              setpgid(99999, 1) == -1 && errno == ESRCH);
}

static void test_process_table_inspection(test_ctx_t *ctx) {
  size_t active = eos_proc_count_active();
  report_test(ctx, "proc: eos_proc_count_active() >= 1", active >= 1);

  eos_proc_t *table = (eos_proc_t *)malloc(EOS_MAX_PROCS * sizeof(eos_proc_t));
  if (table) {
    size_t snap_count = eos_proc_snapshot(table, EOS_MAX_PROCS);
    report_test(ctx, "proc: eos_proc_snapshot() returns non-zero entries", snap_count >= 1);

    bool found_pid1 = false;
    bool found_self = false;
    pid_t self_pid = getpid();

    for (size_t i = 0; i < snap_count; i++) {
      if (table[i].pid == 1) found_pid1 = true;
      if (table[i].pid == self_pid) found_self = true;
    }
    report_test(ctx, "proc: snapshot contains PID 1 (init shell)", found_pid1);
    report_test(ctx, "proc: snapshot contains current process", found_self);
    free(table);
  } else {
    report_test(ctx, "proc: snapshot buffer allocation", false);
  }

  eos_proc_t *self_entry = eos_proc_find(getpid());
  report_test(ctx, "proc: eos_proc_find(getpid()) finds active entry",
              self_entry != NULL && self_entry->pid == getpid());
  report_test(ctx, "proc: eos_proc_find(99999) returns NULL", eos_proc_find(99999) == NULL);
}

static void test_lifecycle_and_waitpid(test_ctx_t *ctx) {
  // Test waitpid with non-existent PID
  report_test(ctx, "proc: waitpid(99999, NULL, 0) returns -1 with ECHILD",
              waitpid(99999, NULL, 0) == -1 && errno == ECHILD);

  // Test waitpid with WNOHANG on non-existent PID
  report_test(ctx, "proc: waitpid(99999, NULL, WNOHANG) returns -1 with ECHILD",
              waitpid(99999, NULL, WNOHANG) == -1 && errno == ECHILD);

  // Test waitpid with WNOHANG on any child (-1)
  int wnohang_ret = waitpid(-1, NULL, WNOHANG);
  report_test(ctx, "proc: waitpid(-1, NULL, WNOHANG) returns 0 or -1 ECHILD",
              wnohang_ret == 0 || (wnohang_ret == -1 && errno == ECHILD));

  // Spawn a child process using execve
  char *child_argv[] = {"test_proc", "--child-exit", "42", NULL};
  int exec_rc = execve("test_proc", child_argv, NULL);
  report_test(ctx, "proc: execve('test_proc', --child-exit 42) succeeds", exec_rc == 0);

  int raw_status = 0;
  pid_t reaped_pid = waitpid(-1, &raw_status, 0);
  report_test(ctx, "proc: waitpid(-1, &status, 0) returns positive child PID", reaped_pid > 0);
  report_test(ctx, "proc: WIFEXITED(status) is true", WIFEXITED(raw_status) != 0);
  report_test(ctx, "proc: WEXITSTATUS(status) == 42", WEXITSTATUS(raw_status) == 42);

  // Test eos_wait wrapper
  char *child_argv2[] = {"test_proc", "--child-exit", "7", NULL};
  exec_rc = execve("test_proc", child_argv2, NULL);
  report_test(ctx, "proc: execve('test_proc', --child-exit 7) succeeds", exec_rc == 0);

  int wait_status = -1;
  int wait_rc = eos_wait(&wait_status);
  report_test(ctx, "proc: eos_wait(&status) succeeds", wait_rc == 0);
  report_test(ctx, "proc: eos_wait extracted exit code == 7", wait_status == 7);
}

static void test_orphan_reparenting(test_ctx_t *ctx) {
  // Create a parent-child relationship manually in process table to verify orphan re-parenting
  pid_t fake_parent = eos_proc_create("fake_p", NULL, 1, 0);
  report_test(ctx, "proc: create fake parent process", fake_parent > 1);

  pid_t fake_child = eos_proc_create("fake_c", NULL, fake_parent, 0);
  report_test(ctx, "proc: create fake child with ppid = fake_parent", fake_child > fake_parent);

  eos_proc_t *fc = eos_proc_find(fake_child);
  report_test(ctx, "proc: child initially references fake_parent",
              fc != NULL && fc->ppid == fake_parent);

  // When fake_parent exits, fake_child must be re-parented to PID 1
  eos_proc_exit(fake_parent, 0);
  report_test(ctx, "proc: orphan re-parented to PID 1 upon parent termination",
              fc != NULL && fc->ppid == 1);

  // Reap both mock processes
  int dummy = 0;
  waitpid(fake_parent, &dummy, 0);
  eos_proc_exit(fake_child, 0);
  waitpid(fake_child, &dummy, 0);
}

static void test_pipeline_and_pgid(test_ctx_t *ctx) {
  // Test pipeline execution with process table integration
  int ret = system("echo proc_test_data | cat > /tmp/proc_pipe.txt");
  report_test(ctx, "proc: pipeline 'echo ... | cat' succeeds with 0", ret == 0);

  FILE *f = fopen("/tmp/proc_pipe.txt", "r");
  char buf[64] = {0};
  if (f) {
    if (fgets(buf, sizeof(buf), f)) {
      char *nl = strchr(buf, '\n');
      if (nl) *nl = '\0';
      char *cr = strchr(buf, '\r');
      if (cr) *cr = '\0';
    }
    fclose(f);
    unlink("/tmp/proc_pipe.txt");
  }
  report_test(ctx, "proc: pipeline output verified", strcmp(buf, "proc_test_data") == 0);
}

static void test_1000_cycles_stress(test_ctx_t *ctx) {
  size_t initial_active = eos_proc_count_active();
  bool all_cycles_ok = true;
  char *child_argv[] = {"test_proc", "--child-exit", "0", NULL};

  printf("  -> Executing 1000 spawn/exit cycles...\n");
  for (int i = 0; i < 1000; i++) {
    if (execve("test_proc", child_argv, NULL) != 0) {
      all_cycles_ok = false;
      printf("  [FAIL] Cycle %d: execve failed\n", i);
      break;
    }
    int status = -1;
    if (eos_wait(&status) != 0 || status != 0) {
      all_cycles_ok = false;
      printf("  [FAIL] Cycle %d: eos_wait failed (status=%d)\n", i, status);
      break;
    }
  }

  size_t final_active = eos_proc_count_active();
  report_test(ctx, "proc: 1000 spawn/exit stress cycles completed cleanly", all_cycles_ok);
  report_test(ctx, "proc: 0 process table growth after 1000 cycles",
              final_active == initial_active);
}

static void test_procfs_process_hierarchy(test_ctx_t *ctx) {
  struct stat st;
  // Test stat on /proc/1
  report_test(ctx, "procfs: stat('/proc/1') succeeds and is a directory",
              stat("/proc/1", &st) == 0 && S_ISDIR(st.st_mode));

  // Test stat on /proc/self
  report_test(ctx, "procfs: stat('/proc/self') succeeds and is a directory",
              stat("/proc/self", &st) == 0 && S_ISDIR(st.st_mode));

  // Test reading /proc/self/status
  FILE *f = fopen("/proc/self/status", "r");
  report_test(ctx, "procfs: fopen('/proc/self/status') succeeds", f != NULL);
  if (f) {
    char buf[512] = {0};
    size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: /proc/self/status contains 'Name:' and 'Pid:'",
                read_bytes > 0 && strstr(buf, "Name:") != NULL && strstr(buf, "Pid:") != NULL);
  }

  // Test reading /proc/self/cmdline
  f = fopen("/proc/self/cmdline", "r");
  report_test(ctx, "procfs: fopen('/proc/self/cmdline') succeeds", f != NULL);
  if (f) {
    char buf[128] = {0};
    size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: /proc/self/cmdline contains process name",
                read_bytes > 0 && (strstr(buf, "test_proc") != NULL || strstr(buf, "ush") != NULL ||
                                   strstr(buf, "proc") != NULL || strstr(buf, "init") != NULL));
  }

  // Test reading /proc/self/cwd
  f = fopen("/proc/self/cwd", "r");
  report_test(ctx, "procfs: fopen('/proc/self/cwd') succeeds", f != NULL);
  if (f) {
    char buf[128] = {0};
    size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: /proc/self/cwd contains path starting with '/'",
                read_bytes > 0 && buf[0] == '/');
  }

  // Test reading /proc/1/stat
  f = fopen("/proc/1/stat", "r");
  report_test(ctx, "procfs: fopen('/proc/1/stat') succeeds", f != NULL);
  if (f) {
    char buf[128] = {0};
    size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: /proc/1/stat contains '1 (init)'",
                read_bytes > 0 && strstr(buf, "1 (init)") != NULL);
  }

  // Test directory listing of /proc
  DIR *d = opendir("/proc");
  report_test(ctx, "procfs: opendir('/proc') succeeds", d != NULL);
  if (d) {
    bool found_pipes = false;
    bool found_self = false;
    bool found_pid1 = false;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
      if (strcmp(ent->d_name, "pipes") == 0) found_pipes = true;
      if (strcmp(ent->d_name, "self") == 0) found_self = true;
      if (strcmp(ent->d_name, "1") == 0) found_pid1 = true;
    }
    closedir(d);
    report_test(ctx, "procfs: readdir('/proc') contains 'pipes'", found_pipes);
    report_test(ctx, "procfs: readdir('/proc') contains 'self'", found_self);
    report_test(ctx, "procfs: readdir('/proc') contains '1'", found_pid1);
  }

  // Test directory listing of /proc/self/fd
  d = opendir("/proc/self/fd");
  report_test(ctx, "procfs: opendir('/proc/self/fd') succeeds", d != NULL);
  if (d) {
    bool found_fd0 = false;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
      if (strcmp(ent->d_name, "0") == 0) found_fd0 = true;
    }
    closedir(d);
    report_test(ctx, "procfs: readdir('/proc/self/fd') contains fd 0", found_fd0);
  }
}

extern int ps_main(int argc, char **argv);
extern int uptime_main(int argc, char **argv);

static void test_ps_utility(test_ctx_t *ctx) {
  printf("--- Testing ps utility ---\n");
  char *help_argv[] = {"ps", "--help", NULL};
  int rc = ps_main(2, help_argv);
  report_test(ctx, "ps: ps --help exits with 0", rc == 0);

  char *default_argv[] = {"ps", NULL};
  rc = ps_main(1, default_argv);
  report_test(ctx, "ps: default ps exits with 0", rc == 0);

  char *full_argv[] = {"ps", "-f", NULL};
  rc = ps_main(2, full_argv);
  report_test(ctx, "ps: ps -f exits with 0", rc == 0);

  char *long_argv[] = {"ps", "-l", NULL};
  rc = ps_main(2, long_argv);
  report_test(ctx, "ps: ps -l exits with 0", rc == 0);

  // Test spawning ps via execve
  char *spawn_argv[] = {"ps", "-ef", NULL};
  int exec_rc = execve("ps", spawn_argv, NULL);
  report_test(ctx, "ps: execve('ps', -ef) succeeds", exec_rc == 0);
  int status = -1;
  pid_t reaped = waitpid(-1, &status, 0);
  report_test(ctx, "ps: waitpid reaped ps process", reaped > 0);
  report_test(ctx, "ps: ps process exited with 0", WIFEXITED(status) && WEXITSTATUS(status) == 0);

  // Verify CPU time in /proc/self/status
  FILE *f = fopen("/proc/self/status", "r");
  report_test(ctx, "procfs: fopen('/proc/self/status') contains CPUTime", f != NULL);
  if (f) {
    char buf[512] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: status contains 'CPUTime:' and 'StartTime:'",
                strstr(buf, "CPUTime:") != NULL && strstr(buf, "StartTime:") != NULL);
  }
}

static void test_loadavg_and_uptime(test_ctx_t *ctx) {
  printf("--- Testing /proc/loadavg, /proc/stat and uptime ---\n");
  FILE *f = fopen("/proc/loadavg", "r");
  report_test(ctx, "procfs: fopen('/proc/loadavg') succeeds", f != NULL);
  if (f) {
    char buf[128] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    double l1 = -1, l5 = -1, l15 = -1;
    unsigned int total = 0;
    int pid = 0;
    int parsed = sscanf(buf, "%lf %lf %lf 1/%u %d", &l1, &l5, &l15, &total, &pid);
    report_test(ctx, "procfs: /proc/loadavg format matches Linux", parsed >= 3 && l1 >= 0.0);
  }

  f = fopen("/proc/stat", "r");
  report_test(ctx, "procfs: fopen('/proc/stat') succeeds", f != NULL);
  if (f) {
    char buf[256] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: /proc/stat contains 'cpu ' and 'processes'",
                strstr(buf, "cpu ") != NULL && strstr(buf, "processes") != NULL);
  }

  char *uptime_argv[] = {"uptime", NULL};
  int rc = uptime_main(1, uptime_argv);
  report_test(ctx, "uptime: uptime exits with 0", rc == 0);

  char *uptime_help[] = {"uptime", "--help", NULL};
  rc = uptime_main(2, uptime_help);
  report_test(ctx, "uptime: uptime --help exits with 0", rc == 0);

  char *spawn_argv[] = {"uptime", NULL};
  int exec_rc = execve("uptime", spawn_argv, NULL);
  report_test(ctx, "uptime: execve('uptime') succeeds", exec_rc == 0);
  int status = -1;
  pid_t reaped = waitpid(-1, &status, 0);
  report_test(ctx, "uptime: waitpid reaped uptime process", reaped > 0);
  report_test(ctx, "uptime: uptime process exited with 0", WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int test_proc_main(int argc, char **argv) {
  // Subcommand dispatch for child processes spawned during tests
  if (argc >= 3 && strcmp(argv[1], "--child-exit") == 0) {
    int code = atoi(argv[2]);
    return code;
  }

  printf("\n=== Running Process Table & Lifecycle Unit Tests ===\n");
  test_ctx_t ctx = {0, 0};

  test_posix_getters_and_setters(&ctx);
  test_process_table_inspection(&ctx);
  test_lifecycle_and_waitpid(&ctx);
  test_orphan_reparenting(&ctx);
  test_pipeline_and_pgid(&ctx);
  test_procfs_process_hierarchy(&ctx);
  test_ps_utility(&ctx);
  test_loadavg_and_uptime(&ctx);
  test_1000_cycles_stress(&ctx);

  printf("\nProcess Table Tests: %d passed, %d failed\n\n", ctx.passed, ctx.failed);
  return (ctx.failed == 0) ? 0 : 1;
}

EOS_BIN_ATTR eos_bin_t test_proc_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "test_proc",
    .name = "Process Table Tests",
    .group = "Tests",
    .flags = 0,
    .stack_size = 16384,
    .description = "Automated test suite for process table, POSIX syscalls and lifecycle",
    .entry_point = test_proc_main,
};