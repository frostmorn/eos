#include "ecore/bin.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static void read_proc_times(uint64_t *out_user, uint64_t *out_sys, uint64_t *out_wait,
                            uint64_t *out_cutime, uint64_t *out_cstime, uint64_t *out_cwait) {
  if (out_user) *out_user = 0;
  if (out_sys) *out_sys = 0;
  if (out_wait) *out_wait = 0;
  if (out_cutime) *out_cutime = 0;
  if (out_cstime) *out_cstime = 0;
  if (out_cwait) *out_cwait = 0;

  FILE *f = fopen("/proc/self/status", "r");
  if (!f) return;

  char line[128];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "UserTime:\t", 10) == 0) {
      if (out_user) *out_user = (uint64_t)strtoull(line + 10, NULL, 10);
    } else if (strncmp(line, "SystemTime:\t", 12) == 0) {
      if (out_sys) *out_sys = (uint64_t)strtoull(line + 12, NULL, 10);
    } else if (strncmp(line, "WaitTime:\t", 10) == 0) {
      if (out_wait) *out_wait = (uint64_t)strtoull(line + 10, NULL, 10);
    } else if (strncmp(line, "ChildrenUserTime:\t", 18) == 0) {
      if (out_cutime) *out_cutime = (uint64_t)strtoull(line + 18, NULL, 10);
    } else if (strncmp(line, "ChildrenSystemTime:\t", 20) == 0) {
      if (out_cstime) *out_cstime = (uint64_t)strtoull(line + 20, NULL, 10);
    } else if (strncmp(line, "ChildrenWaitTime:\t", 18) == 0) {
      if (out_cwait) *out_cwait = (uint64_t)strtoull(line + 18, NULL, 10);
    }
  }
  fclose(f);
}

static void print_time_default(const char *label, uint64_t us) {
  uint32_t total_sec = (uint32_t)(us / 1000000ULL);
  uint32_t rem_ms = (uint32_t)((us % 1000000ULL) / 1000ULL);
  uint32_t minutes = total_sec / 60;
  uint32_t seconds = total_sec % 60;

  printf("%-8s%um%u.%03us\n", label, (unsigned int)minutes, (unsigned int)seconds, (unsigned int)rem_ms);
}

static void print_time_posix(const char *label, uint64_t us) {
  double sec = (double)us / 1000000.0;
  printf("%s %.2f\n", label, sec);
}

int time_main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: time [-p] command [arg...]\n");
    return 1;
  }

  bool posix_format = false;
  int cmd_idx = 1;

  while (cmd_idx < argc && argv[cmd_idx][0] == '-') {
    if (strcmp(argv[cmd_idx], "-p") == 0 || strcmp(argv[cmd_idx], "--portability") == 0) {
      posix_format = true;
      cmd_idx++;
    } else if (strcmp(argv[cmd_idx], "-h") == 0 || strcmp(argv[cmd_idx], "--help") == 0) {
      printf("Usage: time [-p] command [arg...]\n\n");
      printf("Time execution of command and print real, user, and sys time statistics.\n\n");
      printf("Options:\n");
      printf("  -p, --portability   Use POSIX format\n");
      printf("  -h, --help          Display this help and exit\n");
      return 0;
    } else if (strcmp(argv[cmd_idx], "--") == 0) {
      cmd_idx++;
      break;
    } else {
      break;
    }
  }

  if (cmd_idx >= argc) {
    printf("time: missing command to execute\n");
    return 1;
  }

  char cmd_buf[1024];
  cmd_buf[0] = '\0';
  size_t cur_len = 0;

  for (int i = cmd_idx; i < argc; i++) {
    size_t arg_len = strlen(argv[i]);
    if (cur_len + arg_len + 2 >= sizeof(cmd_buf)) {
      printf("time: command arguments too long\n");
      return 1;
    }
    if (cur_len > 0) {
      cmd_buf[cur_len++] = ' ';
      cmd_buf[cur_len] = '\0';
    }
    memcpy(&cmd_buf[cur_len], argv[i], arg_len + 1);
    cur_len += arg_len;
  }

  uint64_t user_before = 0, sys_before = 0, wait_before = 0, cutime_before = 0, cstime_before = 0, cwait_before = 0;
  read_proc_times(&user_before, &sys_before, &wait_before, &cutime_before, &cstime_before, &cwait_before);

  struct timeval t0, t1;
  gettimeofday(&t0, NULL);

  int ret = 0;
  bool use_execve = false;
  bool has_meta = false;
  for (int i = cmd_idx; i < argc; i++) {
    if (strpbrk(argv[i], "|><&;")) {
      has_meta = true;
      break;
    }
  }

  if (!has_meta) {
    int sub_argc = argc - cmd_idx;
    char **sub_argv = malloc((sub_argc + 1) * sizeof(char *));
    if (sub_argv) {
      for (int i = 0; i < sub_argc; i++) {
        sub_argv[i] = argv[cmd_idx + i];
      }
      sub_argv[sub_argc] = NULL;
      if (execve(argv[cmd_idx], sub_argv, NULL) == 0) {
        use_execve = true;
        int status = 0;
        if (waitpid(-1, &status, 0) > 0) {
          ret = WIFEXITED(status) ? WEXITSTATUS(status) : status;
        }
      }
      free(sub_argv);
    }
  }

  if (!use_execve) {
    ret = system(cmd_buf);
  }

  gettimeofday(&t1, NULL);

  uint64_t user_after = 0, sys_after = 0, wait_after = 0, cutime_after = 0, cstime_after = 0, cwait_after = 0;
  read_proc_times(&user_after, &sys_after, &wait_after, &cutime_after, &cstime_after, &cwait_after);

  int64_t sec_diff = (int64_t)t1.tv_sec - (int64_t)t0.tv_sec;
  int64_t usec_diff = (int64_t)t1.tv_usec - (int64_t)t0.tv_usec;
  int64_t total_real = sec_diff * 1000000LL + usec_diff;
  if (total_real < 0) total_real = 0;

  uint64_t real_us = (uint64_t)total_real;
  uint64_t delta_cutime = (cutime_after >= cutime_before) ? (cutime_after - cutime_before) : 0;
  uint64_t delta_cstime = (cstime_after >= cstime_before) ? (cstime_after - cstime_before) : 0;
  uint64_t delta_cwait = (cwait_after >= cwait_before) ? (cwait_after - cwait_before) : 0;
  uint64_t delta_user = (user_after >= user_before) ? (user_after - user_before) : 0;
  uint64_t delta_sys = (sys_after >= sys_before) ? (sys_after - sys_before) : 0;
  uint64_t delta_wait = (wait_after >= wait_before) ? (wait_after - wait_before) : 0;

  uint64_t user_us = (delta_cutime > 0 || delta_cstime > 0 || delta_cwait > 0) ? delta_cutime : delta_user;
  uint64_t sys_us = (delta_cutime > 0 || delta_cstime > 0 || delta_cwait > 0) ? delta_cstime : delta_sys;
  uint64_t wait_us = (delta_cutime > 0 || delta_cstime > 0 || delta_cwait > 0) ? delta_cwait : delta_wait;

  printf("\n");
  if (posix_format) {
    print_time_posix("real", real_us);
    print_time_posix("user", user_us);
    print_time_posix("sys", sys_us);
  } else {
    print_time_default("real", real_us);
    print_time_default("user", user_us);
    print_time_default("sys", sys_us);
    print_time_default("wait", wait_us);
  }

  return ret;
}

EOS_BIN_ATTR eos_bin_t time_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "time",
    .name = "time",
    .group = "System",
    .description = "Time command execution (real, user, sys)",
    .entry_point = time_main,
};
