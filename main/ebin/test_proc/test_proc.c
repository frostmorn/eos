#include "ecore/bin.h"
#include "ecore/proc.h"
#include "emisc/strlimits.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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
  unlink("/tmp/proc_pipe.txt");
  int ret = system("echo proc_test_data | cat > /tmp/proc_pipe.txt");
  report_test(ctx, "proc: pipeline 'echo ... | cat' succeeds with 0", ret == 0);

  FILE *f = fopen("/tmp/proc_pipe.txt", "r");
  char buf[EOS_SMALL_STR_LEN] = {0};
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
  while (waitpid(-1, NULL, WNOHANG) > 0);
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
    usleep((i % 20 == 0) ? 10000 : 1000);
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
    char buf[EOS_LARGE_STR_LEN] = {0};
    size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: /proc/self/status contains 'Name:' and 'Pid:'",
                read_bytes > 0 && strstr(buf, "Name:") != NULL && strstr(buf, "Pid:") != NULL);
  }

  // Test reading /proc/self/cmdline
  f = fopen("/proc/self/cmdline", "r");
  report_test(ctx, "procfs: fopen('/proc/self/cmdline') succeeds", f != NULL);
  if (f) {
    char buf[EOS_MID_STR_LEN] = {0};
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
    char buf[EOS_MID_STR_LEN] = {0};
    size_t read_bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    report_test(ctx, "procfs: /proc/self/cwd contains path starting with '/'",
                read_bytes > 0 && buf[0] == '/');
  }

  // Test reading /proc/1/stat
  f = fopen("/proc/1/stat", "r");
  report_test(ctx, "procfs: fopen('/proc/1/stat') succeeds", f != NULL);
  if (f) {
    char buf[EOS_MID_STR_LEN] = {0};
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
    char buf[EOS_LARGE_STR_LEN] = {0};
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
    char buf[EOS_MID_STR_LEN] = {0};
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
    char buf[EOS_MID_STR_LEN] = {0};
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

static void *cross_thread_free_worker(void *arg) {
  void *ptr = arg;
  free(ptr);
  return NULL;
}

static size_t get_proc_heap_metric(pid_t pid, const char *key) {
  char path[EOS_SMALL_STR_LEN];
  if (pid == 0) {
    snprintf(path, sizeof(path), "/proc/0/status");
  } else {
    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
  }
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[EOS_MID_STR_LEN];
  size_t val = 0;
  size_t klen = strlen(key);
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, key, klen) == 0) {
      val = (size_t)strtoul(line + klen, NULL, 10);
      break;
    }
  }
  fclose(f);
  return val;
}

static void test_memory_ownership_f02(test_ctx_t *ctx) {
  printf("\n--- F02: Memory Ownership & Tracking Unit Tests ---\n");

  // 1. Cross-thread free
  size_t sram_before = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
  void *shared_buf = malloc(256);
  report_test(ctx, "f02: malloc(256) succeeds", shared_buf != NULL);
  if (shared_buf) {
    size_t sram_during = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
    memset(shared_buf, 0xA5, 256);
    pthread_t th;
    int pt_rc = pthread_create(&th, NULL, cross_thread_free_worker, shared_buf);
    report_test(ctx, "f02: cross-thread free thread created", pt_rc == 0);
    if (pt_rc == 0) {
      pthread_join(th, NULL);
      size_t sram_after = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
      report_test(ctx, "f02: cross-thread free decrements process heap tracking counters",
                  sram_after < sram_during || sram_after <= sram_before);
    } else {
      free(shared_buf);
    }
  }

  // 2. Memory adoption (eos_mem_adopt)
  size_t my_before = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
  size_t k_before = get_proc_heap_metric(0, "HeapSRAM:\t");
  void *adopt_buf = malloc(128);
  report_test(ctx, "f02: malloc(128) for adoption succeeds", adopt_buf != NULL);
  if (adopt_buf) {
    size_t my_during = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
    // Adopt into kernel context (PID 0)
    int rc0 = eos_mem_adopt(adopt_buf, 0);
    report_test(ctx, "f02: eos_mem_adopt(ptr, 0) into kernel context returns 0", rc0 == 0);
    size_t my_after_k = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
    size_t k_after = get_proc_heap_metric(0, "HeapSRAM:\t");
    report_test(ctx, "f02: adopt into kernel context shifts accounting from process to kernel",
                my_after_k < my_during && k_after >= k_before);

    // Adopt back into current process
    int rc1 = eos_mem_adopt(adopt_buf, getpid());
    report_test(ctx, "f02: eos_mem_adopt(ptr, getpid()) returns 0", rc1 == 0);

    // Invalid adoption targets
    report_test(ctx, "f02: eos_mem_adopt(NULL, 0) returns -1 with EINVAL",
                eos_mem_adopt(NULL, 0) == -1 && errno == EINVAL);
    report_test(ctx, "f02: eos_mem_adopt(ptr, 99999) returns -1 with ESRCH",
                eos_mem_adopt(adopt_buf, 99999) == -1 && errno == ESRCH);

    size_t my_before_free = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
    free(adopt_buf);
    size_t my_after_free = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
    report_test(ctx, "f02: free adopted buffer decrements process heap counters",
                my_after_free < my_before_free || my_after_free <= my_before);
  }

  // 3. /proc/0/status (Kernel process context)
  FILE *f0 = fopen("/proc/0/status", "r");
  report_test(ctx, "f02: /proc/0/status opens successfully", f0 != NULL);
  if (f0) {
    char line[EOS_MID_STR_LEN];
    bool has_name_kernel = false;
    bool has_pid_0 = false;
    bool has_sram = false;
    bool has_psram = false;
    while (fgets(line, sizeof(line), f0)) {
      if (strstr(line, "Name:\tkernel")) has_name_kernel = true;
      if (strstr(line, "Pid:\t0")) has_pid_0 = true;
      if (strstr(line, "HeapSRAM:")) has_sram = true;
      if (strstr(line, "HeapPSRAM:")) has_psram = true;
    }
    fclose(f0);
    report_test(ctx, "f02: /proc/0/status contains Name: kernel", has_name_kernel);
    report_test(ctx, "f02: /proc/0/status contains Pid: 0", has_pid_0);
    report_test(ctx, "f02: /proc/0/status contains HeapSRAM", has_sram);
    report_test(ctx, "f02: /proc/0/status contains HeapPSRAM", has_psram);
  }

  // 4. /proc/self/status VmData & Heap stats
  FILE *fself = fopen("/proc/self/status", "r");
  report_test(ctx, "f02: /proc/self/status opens successfully", fself != NULL);
  if (fself) {
    char line[EOS_MID_STR_LEN];
    bool has_vmdata = false;
    bool has_fdsize = false;
    while (fgets(line, sizeof(line), fself)) {
      if (strstr(line, "VmData:")) has_vmdata = true;
      if (strstr(line, "FDSize:")) has_fdsize = true;
    }
    fclose(fself);
    report_test(ctx, "f02: /proc/self/status contains VmData", has_vmdata);
    report_test(ctx, "f02: /proc/self/status contains FDSize", has_fdsize);
  }

  // 5. Per-process memory isolation on exit: child allocates memory, exits, parent memory intact
  void *parent_canary = malloc(256);
  report_test(ctx, "f02: parent canary allocation succeeds", parent_canary != NULL);
  if (parent_canary) {
    memset(parent_canary, 0x3C, 256);

    char *spawn_argv[] = {"test_proc", "--child-mem-leak", NULL};
    int exec_rc = execve("test_proc", spawn_argv, NULL);
    report_test(ctx, "f02: spawn child process for memory isolation test succeeds", exec_rc == 0);

    int status = -1;
    pid_t reaped = waitpid(-1, &status, 0);
    report_test(ctx, "f02: child process reaped", reaped > 0);
    report_test(ctx, "f02: child exited with expected code 42", WIFEXITED(status) && WEXITSTATUS(status) == 42);

    // Verify parent canary is completely uncorrupted
    bool canary_ok = true;
    uint8_t *b = (uint8_t *)parent_canary;
    for (int i = 0; i < 256; i++) {
      if (b[i] != 0x3C) {
        canary_ok = false;
        break;
      }
    }
    report_test(ctx, "f02: parent memory unaffected and uncorrupted after child exit", canary_ok);
    free(parent_canary);
  }

  // 6. Attach driver / kernel state from short-lived child process followed by parent use
  char *attach_argv[] = {"test_proc", "--child-attach-driver", NULL};
  int attach_rc = execve("test_proc", attach_argv, NULL);
  report_test(ctx, "f02: spawn child for driver attach test succeeds", attach_rc == 0);

  int attach_status = -1;
  pid_t attach_reaped = waitpid(-1, &attach_status, 0);
  report_test(ctx, "f02: child attach process reaped", attach_reaped > 0);
  report_test(ctx, "f02: child attach process exited successfully", WIFEXITED(attach_status) && WEXITSTATUS(attach_status) == 0);

  // Churn and poison heap to detect any use-after-free
  void *poison_bufs[8];
  for (int p = 0; p < 8; p++) {
    poison_bufs[p] = malloc(256);
    if (poison_bufs[p]) memset(poison_bufs[p], 0xDD, 256);
  }
  for (int p = 0; p < 8; p++) {
    free(poison_bufs[p]);
  }

  void *drv_ptr = NULL;
  FILE *f_ptr = fopen("/tmp_drv_ptr", "r");
  if (f_ptr) {
    if (fscanf(f_ptr, "%p", &drv_ptr) == 1) {
      // successfully parsed
    }
    fclose(f_ptr);
    unlink("/tmp_drv_ptr");
  }

  report_test(ctx, "f02: driver state pointer retrieved from child", drv_ptr != NULL);
  if (drv_ptr) {
    bool valid_state = true;
    uint8_t *db = (uint8_t *)drv_ptr;
    for (int i = 0; i < 128; i++) {
      if (db[i] != 0x5A) {
        valid_state = false;
        break;
      }
    }
    report_test(ctx, "f02: driver state remains intact in kernel context (PID 0) after child exit and heap churn", valid_state);

    int adopt_rc = eos_mem_adopt(drv_ptr, getpid());
    report_test(ctx, "f02: adopt driver state into parent process succeeds", adopt_rc == 0);
    size_t sram_before_drv_free = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
    free(drv_ptr);
    size_t sram_after_drv_free = get_proc_heap_metric(getpid(), "HeapSRAM:\t");
    report_test(ctx, "f02: free driver state buffer decrements process heap tracking",
                sram_after_drv_free < sram_before_drv_free || sram_after_drv_free <= my_before);
  }
}

static void *worker_thread_fn(void *arg) {
  volatile int *cnt = (volatile int *)arg;
  while (1) {
    if (cnt) (*cnt)++;
    usleep(10 * 1000);
  }
  return NULL;
}

static void test_bg_process_and_threads(test_ctx_t *ctx) {
  // 1. Background process launch via shell (&)
  unlink("/tmp/test_bg_pid.txt");
  system("test_proc --child-bg &");
  int bg_pid = -1;
  for (int i = 0; i < 25; i++) {
    FILE *f = fopen("/tmp/test_bg_pid.txt", "r");
    if (f) {
      if (fscanf(f, "%d", &bg_pid) == 1) {
        fclose(f);
        break;
      }
      fclose(f);
    }
    usleep(20 * 1000);
  }
  report_test(ctx, "proc: background command runs with separate PID", bg_pid > 1 && bg_pid != getpid());
  if (bg_pid > 1) {
    char proc_path[EOS_SMALL_STR_LEN];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", bg_pid);
    FILE *fp = fopen(proc_path, "r");
    report_test(ctx, "proc: background process visible in /proc", fp != NULL);
    if (fp) fclose(fp);
    int bg_status = 0;
    waitpid(bg_pid, &bg_status, 0);
  }
  unlink("/tmp/test_bg_pid.txt");

  // 2. Background threads termination upon process exit
  unlink("/tmp/test_cnt.txt");
  char *t_argv[] = {"test_proc", "--child-spawn-thread", NULL};
  int exec_rc = execve("test_proc", t_argv, NULL);
  report_test(ctx, "proc: execve child with background thread succeeds", exec_rc == 0);

  int t_status = 0;
  pid_t reaped_pid = waitpid(-1, &t_status, 0);
  report_test(ctx, "proc: waitpid reaped child with background thread", reaped_pid > 0);

  volatile int *cnt_ptr = NULL;
  FILE *fc = fopen("/tmp/test_cnt.txt", "r");
  if (fc) {
    if (fscanf(fc, "%p", (void **)&cnt_ptr) == 1) {}
    fclose(fc);
    unlink("/tmp/test_cnt.txt");
  }
  report_test(ctx, "proc: retrieved worker counter address", cnt_ptr != NULL);
  if (cnt_ptr) {
    int c1 = *cnt_ptr;
    usleep(50 * 1000);
    int c2 = *cnt_ptr;
    report_test(ctx, "proc: background thread terminated upon main thread exit", c1 == c2 && c1 > 0);
    free((void *)cnt_ptr);
  }
}

typedef struct {
  int fd;
  volatile int started;
  ssize_t read_res;
  int err;
  volatile int finished;
} vfs_reader_ctx_t;

static void *vfs_blocked_reader_worker(void *arg) {
  vfs_reader_ctx_t *tc = (vfs_reader_ctx_t *)arg;
  tc->started = 1;
  char buf[32];
  tc->read_res = read(tc->fd, buf, sizeof(buf));
  tc->err = errno;
  tc->finished = 1;
  return NULL;
}

typedef struct {
  int fd;
  volatile int started;
  ssize_t read_res;
  char buf[32];
  volatile int finished;
} vfs_fullduplex_ctx_t;

static void *vfs_fullduplex_reader_worker(void *arg) {
  vfs_fullduplex_ctx_t *tc = (vfs_fullduplex_ctx_t *)arg;
  tc->started = 1;
  memset(tc->buf, 0, sizeof(tc->buf));
  tc->read_res = read(tc->fd, tc->buf, 4);
  tc->finished = 1;
  return NULL;
}

typedef struct {
  int id;
  int iterations;
  int success_count;
} vfs_churn_ctx_t;

static void *vfs_churn_worker(void *arg) {
  vfs_churn_ctx_t *c = (vfs_churn_ctx_t *)arg;
  char path[EOS_SMALL_STR_LEN];
  snprintf(path, sizeof(path), "/tmp/churn_%d.txt", c->id);
  c->success_count = 0;

  for (int i = 0; i < c->iterations; i++) {
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) break;
    char wbuf[16];
    int wlen = snprintf(wbuf, sizeof(wbuf), "c_%d_%d", c->id, i);
    if (write(fd, wbuf, wlen) != wlen) {
      close(fd);
      break;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
      close(fd);
      break;
    }
    char rbuf[16] = {0};
    if (read(fd, rbuf, sizeof(rbuf)) != wlen) {
      close(fd);
      break;
    }
    if (close(fd) != 0) {
      break;
    }
    unlink(path);
    c->success_count++;
  }
  return NULL;
}

static void test_vfs_concurrency(test_ctx_t *ctx) {
  printf("\n--- VFS Concurrency, Lifecycle & Non-Blocking Reaping Tests ---\n");

  // 1. Blocked reader + pipe write-end close from another thread -> wakes with EOF (0)
  int p_fds[2];
  int p_rc = pipe(p_fds);
  report_test(ctx, "vfs: pipe() creates pair of fds", p_rc == 0);
  if (p_rc == 0) {
    vfs_reader_ctx_t r_ctx = {.fd = p_fds[0], .started = 0, .read_res = -999, .finished = 0};
    pthread_t th;
    int th_rc = pthread_create(&th, NULL, vfs_blocked_reader_worker, &r_ctx);
    report_test(ctx, "vfs: blocked reader thread created", th_rc == 0);
    if (th_rc == 0) {
      while (!r_ctx.started) usleep(1000);
      usleep(20 * 1000); // 20ms to ensure blocked inside pipefs_read

      // Close the write end - this must wake the reader with EOF (0)
      close(p_fds[1]);

      for (int i = 0; i < 20 && !r_ctx.finished; i++) {
        usleep(10 * 1000);
      }
      report_test(ctx, "vfs: closing pipe write-end wakes blocked reader promptly", r_ctx.finished == 1);
      report_test(ctx, "vfs: blocked reader receives EOF (0 bytes)", r_ctx.read_res == 0);
      pthread_join(th, NULL);
      close(p_fds[0]);
    } else {
      close(p_fds[0]);
      close(p_fds[1]);
    }
  }

  // 2. Blocked reader + pipe read-end close from another thread -> wakes promptly
  p_rc = pipe(p_fds);
  if (p_rc == 0) {
    vfs_reader_ctx_t r_ctx = {.fd = p_fds[0], .started = 0, .read_res = -999, .finished = 0};
    pthread_t th;
    if (pthread_create(&th, NULL, vfs_blocked_reader_worker, &r_ctx) == 0) {
      while (!r_ctx.started) usleep(1000);
      usleep(20 * 1000);

      // Close read end from this thread while worker is inside read()
      close(p_fds[0]);

      for (int i = 0; i < 20 && !r_ctx.finished; i++) {
        usleep(10 * 1000);
      }
      report_test(ctx, "vfs: closing pipe read-end wakes blocked reader", r_ctx.finished == 1);
      report_test(ctx, "vfs: reader unblocks with EOF or error", r_ctx.read_res <= 0);
      pthread_join(th, NULL);
      close(p_fds[1]);
    } else {
      close(p_fds[0]);
      close(p_fds[1]);
    }
  }

  // 3. Full-duplex read and write on ONE fd
  int fd_duplex = open("/proc/pipes/test_fullduplex", O_RDWR | O_CREAT | O_TRUNC, 0666);
  report_test(ctx, "vfs: open O_RDWR bidirectional pipe succeeds", fd_duplex >= 0);
  if (fd_duplex >= 0) {
    vfs_fullduplex_ctx_t d_ctx = {.fd = fd_duplex, .started = 0, .read_res = -1, .finished = 0};
    pthread_t th;
    if (pthread_create(&th, NULL, vfs_fullduplex_reader_worker, &d_ctx) == 0) {
      while (!d_ctx.started) usleep(1000);
      usleep(20 * 1000); // reader blocked in read() on fd_duplex

      // Write to the SAME fd while reader is blocked
      ssize_t wn = write(fd_duplex, "ping", 4);
      report_test(ctx, "vfs: write on same fd succeeds while reader is blocked", wn == 4);

      for (int i = 0; i < 20 && !d_ctx.finished; i++) {
        usleep(10 * 1000);
      }
      report_test(ctx, "vfs: full-duplex reader unblocks upon write", d_ctx.finished == 1);
      report_test(ctx, "vfs: full-duplex reader received correct data",
                  d_ctx.read_res == 4 && memcmp(d_ctx.buf, "ping", 4) == 0);
      pthread_join(th, NULL);
    }
    close(fd_duplex);
    unlink("/proc/pipes/test_fullduplex");
  }

  // 4. Multi-threaded FD reuse stress (4 threads churn open/write/lseek/read/close concurrently)
  int fperr = fprintf(stderr, "[TEST_FPRINTF_STDERR]\n");
  fflush(stderr);
  report_test(ctx, "vfs: fprintf to stderr succeeds", fperr > 0);
  ssize_t w2 = write(2, "[TEST_STDERR_WRITE]\n", 20);
  report_test(ctx, "vfs: write to stderr (fd 2) succeeds", w2 == 20);

  #define CHURN_THREADS 4
  #define CHURN_ITERS 25
  pthread_t churn_ths[CHURN_THREADS];
  vfs_churn_ctx_t churn_ctxs[CHURN_THREADS];
  bool churn_spawned = true;
  for (int i = 0; i < CHURN_THREADS; i++) {
    churn_ctxs[i].id = i;
    churn_ctxs[i].iterations = CHURN_ITERS;
    churn_ctxs[i].success_count = 0;
    if (pthread_create(&churn_ths[i], NULL, vfs_churn_worker, &churn_ctxs[i]) != 0) {
      churn_spawned = false;
    }
  }
  report_test(ctx, "vfs: concurrent fd churn threads started", churn_spawned);
  bool all_churn_ok = churn_spawned;
  for (int i = 0; i < CHURN_THREADS; i++) {
    if (churn_spawned) {
      pthread_join(churn_ths[i], NULL);
      if (churn_ctxs[i].success_count != CHURN_ITERS) {
        all_churn_ok = false;
      }
    }
  }
  report_test(ctx, "vfs: 100 concurrent open/write/read/close cycles succeeded with 0 races", all_churn_ok);
  #undef CHURN_THREADS
  #undef CHURN_ITERS

  // 5. Process reaping with a blocked reader thread
  char *br_argv[] = {"test_proc", "--child-blocked-reader", NULL};
  int br_exec = execve("test_proc", br_argv, NULL);
  report_test(ctx, "vfs: execve child with blocked reader thread succeeds", br_exec == 0);

  int br_status = 0;
  pid_t br_reaped = waitpid(-1, &br_status, 0);
  report_test(ctx, "vfs: waitpid reaped child with blocked reader without stall", br_reaped > 0);
  report_test(ctx, "vfs: child with blocked reader exited with expected code 33",
              WIFEXITED(br_status) && WEXITSTATUS(br_status) == 33);
}

static void test_cpu_time_accounting_t09(test_ctx_t *ctx) {
  printf("--- Testing T09: CPU time accounting and time ebin ---\n");

  // 1. Test /proc/stat format (cpu line with user, sys, idle, irq + irq line)
  FILE *fstat = fopen("/proc/stat", "r");
  report_test(ctx, "procfs: fopen('/proc/stat') succeeds", fstat != NULL);
  if (fstat) {
    char line[256];
    bool has_cpu = false;
    bool has_irq = false;
    unsigned long u = 0, n = 0, s = 0, id = 0, io = 0, irq = 0;
    while (fgets(line, sizeof(line), fstat)) {
      if (strncmp(line, "cpu ", 4) == 0) {
        int parsed = sscanf(line, "cpu %lu %lu %lu %lu %lu %lu", &u, &n, &s, &id, &io, &irq);
        if (parsed >= 4) {
          has_cpu = true;
        }
      } else if (strncmp(line, "irq ", 4) == 0) {
        has_irq = true;
      }
    }
    fclose(fstat);
    report_test(ctx, "procfs: /proc/stat cpu line has user, sys, idle counters", has_cpu);
    report_test(ctx, "procfs: /proc/stat has separate irq line", has_irq);
  }

  // 2. Test /proc/self/stat utime (field 14) and stime (field 15)
  FILE *fself_stat = fopen("/proc/self/stat", "r");
  report_test(ctx, "procfs: fopen('/proc/self/stat') succeeds", fself_stat != NULL);
  if (fself_stat) {
    char buf[256];
    if (fgets(buf, sizeof(buf), fself_stat)) {
      char *tok = strtok(buf, " ");
      int field = 1;
      bool parsed_fields = false;
      while (tok) {
        if (field == 15) {
          parsed_fields = true;
          break;
        }
        tok = strtok(NULL, " ");
        field++;
      }
      report_test(ctx, "procfs: /proc/self/stat utime and stime fields parsed", parsed_fields);
    }
    fclose(fself_stat);
  }

  // 3. Test /proc/self/status UserTime, SystemTime, WaitTime, ChildrenUserTime
  FILE *fself_status = fopen("/proc/self/status", "r");
  report_test(ctx, "procfs: fopen('/proc/self/status') succeeds", fself_status != NULL);
  if (fself_status) {
    char line[128];
    bool has_user = false, has_sys = false, has_wait = false, has_cutime = false;
    while (fgets(line, sizeof(line), fself_status)) {
      if (strncmp(line, "UserTime:\t", 10) == 0) has_user = true;
      if (strncmp(line, "SystemTime:\t", 12) == 0) has_sys = true;
      if (strncmp(line, "WaitTime:\t", 10) == 0) has_wait = true;
      if (strncmp(line, "ChildrenUserTime:\t", 18) == 0) has_cutime = true;
    }
    fclose(fself_status);
    report_test(ctx, "procfs: /proc/self/status exposes UserTime and SystemTime", has_user && has_sys);
    report_test(ctx, "procfs: /proc/self/status exposes WaitTime and ChildrenUserTime", has_wait && has_cutime);
  }

  // 4. Test CPU time accounting with CPU-bound child
  uint64_t cutime_before = 0, cstime_before = 0;
  FILE *f_stat = fopen("/proc/self/status", "r");
  if (f_stat) {
    char line[128];
    while (fgets(line, sizeof(line), f_stat)) {
      if (strncmp(line, "ChildrenUserTime:\t", 18) == 0) cutime_before = strtoull(line + 18, NULL, 10);
      if (strncmp(line, "ChildrenSystemTime:\t", 20) == 0) cstime_before = strtoull(line + 20, NULL, 10);
    }
    fclose(f_stat);
  }

  char *child_argv[] = {"test_proc", "--child-cpu-bound", NULL};
  int child_exec = execve("test_proc", child_argv, NULL);
  report_test(ctx, "cpu_time: execve CPU-bound child succeeds", child_exec == 0);

  int child_status = 0;
  pid_t child_reaped = waitpid(-1, &child_status, 0);
  report_test(ctx, "cpu_time: waitpid reaped CPU-bound child", child_reaped > 0);
  report_test(ctx, "cpu_time: CPU-bound child process completed",
              WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);

  uint64_t cutime_after = 0, cstime_after = 0;
  f_stat = fopen("/proc/self/status", "r");
  if (f_stat) {
    char line[128];
    while (fgets(line, sizeof(line), f_stat)) {
      if (strncmp(line, "ChildrenUserTime:\t", 18) == 0) cutime_after = strtoull(line + 18, NULL, 10);
      if (strncmp(line, "ChildrenSystemTime:\t", 20) == 0) cstime_after = strtoull(line + 20, NULL, 10);
    }
    fclose(f_stat);
  }

  uint64_t delta_u = (cutime_after >= cutime_before) ? (cutime_after - cutime_before) : 0;
  uint64_t delta_s = (cstime_after >= cstime_before) ? (cstime_after - cstime_before) : 0;
  uint64_t total_c = delta_u + delta_s;
  report_test(ctx, "cpu_time: CPU-bound child accumulated CPU time > 0", total_c > 0);
  report_test(ctx, "cpu_time: CPU-bound child time dominated by user CPU share", delta_u >= (total_c / 2));

  // 5. Test time utility ebin execution
  int time_rc = system("time -p test_proc --child-exit 0");
  report_test(ctx, "time: 'time -p test_proc --child-exit 0' executed successfully", time_rc == 0);
}

int test_proc_main(int argc, char **argv) {
  // Subcommand dispatch for child processes spawned during tests
  if (argc >= 2 && strcmp(argv[1], "--child-cpu-bound") == 0) {
    volatile uint32_t count = 0;
    for (uint32_t i = 0; i < 2000000; i++) {
      count += i ^ (count >> 1);
    }
    (void)count;
    return 0;
  }

  if (argc >= 2 && strcmp(argv[1], "--child-sleep-bound") == 0) {
    usleep(100 * 1000);
    return 0;
  }
  // Subcommand dispatch for child processes spawned during tests
  if (argc >= 3 && strcmp(argv[1], "--child-exit") == 0) {
    int code = atoi(argv[2]);
    return code;
  }

  if (argc >= 2 && strcmp(argv[1], "--child-bg") == 0) {
    FILE *f = fopen("/tmp/test_bg_pid.txt", "w");
    if (f) {
      fprintf(f, "%d\n", (int)getpid());
      fclose(f);
    }
    usleep(500 * 1000);
    return 0;
  }

  if (argc >= 2 && strcmp(argv[1], "--child-spawn-thread") == 0) {
    bool prev = eos_proc_enter_kernel();
    int *cnt = (int *)malloc(sizeof(int));
    if (cnt) *cnt = 1;
    eos_proc_leave_kernel(prev);

    pthread_t th;
    pthread_create(&th, NULL, worker_thread_fn, (void *)cnt);
    FILE *f = fopen("/tmp/test_cnt.txt", "w");
    if (f) {
      fprintf(f, "%p\n", (void *)cnt);
      fclose(f);
    }
    usleep(50 * 1000);
    return 0;
  }

  if (argc >= 2 && strcmp(argv[1], "--child-mem-leak") == 0) {
    for (int i = 0; i < 5; i++) {
      void *m = malloc(100);
      if (m) memset(m, 0x7E, 100);
    }
    return 42;
  }

  if (argc >= 2 && strcmp(argv[1], "--child-attach-driver") == 0) {
    bool prev = eos_proc_enter_kernel();
    void *drv_state = malloc(128);
    if (drv_state) {
      memset(drv_state, 0x5A, 128);
    }
    eos_proc_leave_kernel(prev);
    FILE *f = fopen("/tmp_drv_ptr", "w");
    if (f) {
      fprintf(f, "%p\n", drv_state);
      fclose(f);
    }
    return (drv_state != NULL) ? 0 : 1;
  }

  if (argc >= 2 && strcmp(argv[1], "--child-blocked-reader") == 0) {
    int fds[2];
    if (pipe(fds) < 0) return 1;
    vfs_reader_ctx_t *r_ctx = malloc(sizeof(vfs_reader_ctx_t));
    if (!r_ctx) return 1;
    r_ctx->fd = fds[0];
    r_ctx->started = 0;
    r_ctx->read_res = -1;
    r_ctx->finished = 0;
    pthread_t th;
    pthread_create(&th, NULL, vfs_blocked_reader_worker, r_ctx);
    while (!r_ctx->started) usleep(1000);
    usleep(15 * 1000);
    return 33;
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
  test_memory_ownership_f02(&ctx);
  test_bg_process_and_threads(&ctx);
  test_vfs_concurrency(&ctx);
  test_cpu_time_accounting_t09(&ctx);
  test_1000_cycles_stress(&ctx);

  printf("\nProcess Table Tests: %d passed, %d failed\n\n", ctx.passed, ctx.failed);
  return (ctx.failed == 0) ? 0 : 1;
}

EOS_BIN_ATTR eos_bin_t test_proc_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "test_proc",
    .name = "Process Table Tests",
    .group = "Tests",
    .flags = EOS_BIN_FLAG_STACK_PSRAM,
    .stack_size = 16384,
    .description = "Automated test suite for process table, POSIX syscalls and lifecycle",
    .entry_point = test_proc_main,
};