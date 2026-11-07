#include "ecore/bin.h"
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  int pid;
  int ppid;
  char state_c;
  char name[32];
  char tty[16];
  char cmdline[64];
  uint64_t cpu_time_us;
  uint64_t start_time_us;
} ps_proc_info_t;

static int compare_procs(const void *a, const void *b) {
  const ps_proc_info_t *pa = (const ps_proc_info_t *)a;
  const ps_proc_info_t *pb = (const ps_proc_info_t *)b;
  return pa->pid - pb->pid;
}

static bool parse_proc_status(int pid, ps_proc_info_t *info) {
  char path[32];
  snprintf(path, sizeof(path), "/proc/%d/status", pid);

  FILE *f = fopen(path, "r");
  if (!f) return false;

  memset(info, 0, sizeof(*info));
  info->pid = pid;
  info->state_c = 'R';
  strlcpy(info->tty, "?", sizeof(info->tty));

  char line[128];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "Name:\t", 6) == 0) {
      char *val = line + 6;
      val[strcspn(val, "\r\n")] = '\0';
      strlcpy(info->name, val, sizeof(info->name));
    } else if (strncmp(line, "State:\t", 7) == 0) {
      info->state_c = line[7];
    } else if (strncmp(line, "PPid:\t", 6) == 0) {
      info->ppid = atoi(line + 6);
    } else if (strncmp(line, "TTY:\t", 5) == 0) {
      char *val = line + 5;
      val[strcspn(val, "\r\n")] = '\0';
      if (val[0] != '\0') {
        strlcpy(info->tty, val, sizeof(info->tty));
      }
    } else if (strncmp(line, "CPUTime:\t", 9) == 0) {
      info->cpu_time_us = (uint64_t)strtoull(line + 9, NULL, 10);
    } else if (strncmp(line, "StartTime:\t", 11) == 0) {
      info->start_time_us = (uint64_t)strtoull(line + 11, NULL, 10);
    }
  }
  fclose(f);

  // Read cmdline
  snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
  f = fopen(path, "r");
  if (f) {
    if (fgets(info->cmdline, sizeof(info->cmdline), f)) {
      info->cmdline[strcspn(info->cmdline, "\r\n")] = '\0';
    }
    fclose(f);
  }

  if (info->cmdline[0] == '\0') {
    if (info->name[0] != '\0') {
      snprintf(info->cmdline, sizeof(info->cmdline), "[%s]", info->name);
    } else {
      snprintf(info->cmdline, sizeof(info->cmdline), "[%d]", info->pid);
    }
  }

  return true;
}

static void print_time_formatted(char *buf, size_t size, uint64_t us) {
  uint64_t total_sec = us / 1000000;
  unsigned int hours = (unsigned int)(total_sec / 3600);
  unsigned int mins = (unsigned int)((total_sec % 3600) / 60);
  unsigned int secs = (unsigned int)(total_sec % 60);
  snprintf(buf, size, "%02u:%02u:%02u", hours, mins, secs);
}

static void print_stime_formatted(char *buf, size_t size, uint64_t us) {
  uint64_t total_sec = us / 1000000;
  unsigned int mins = (unsigned int)(total_sec / 60);
  unsigned int secs = (unsigned int)(total_sec % 60);
  if (mins >= 60) {
    unsigned int hours = mins / 60;
    mins %= 60;
    snprintf(buf, size, "%02u:%02u", hours, mins);
  } else {
    snprintf(buf, size, "%02u:%02u", mins, secs);
  }
}

int ps_main(int argc, char **argv) {
  bool full_format = false;
  bool long_format = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-ef") == 0) {
      full_format = true;
    } else if (strcmp(argv[i], "-l") == 0) {
      long_format = true;
    } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "-A") == 0 || strcmp(argv[i], "-a") == 0) {
      // In EOS single-user environment, all processes are listed by default
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: ps [options]\n\n");
      printf("Report process status.\n\n");
      printf("Options:\n");
      printf("  -e, -A       Select all processes (default)\n");
      printf("  -f, -ef      Full-format listing\n");
      printf("  -l           Long-format listing\n");
      printf("  -h, --help   Display this help and exit\n");
      return 0;
    }
  }

  DIR *d = opendir("/proc");
  if (!d) {
    perror("ps: cannot open /proc");
    return 1;
  }

  size_t cap = 16;
  size_t count = 0;
  ps_proc_info_t *procs = (ps_proc_info_t *)malloc(cap * sizeof(ps_proc_info_t));
  if (!procs) {
    closedir(d);
    perror("ps: allocation failed");
    return 1;
  }

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (!isdigit((unsigned char)ent->d_name[0])) {
      continue;
    }
    int pid = atoi(ent->d_name);
    if (pid <= 0) continue;

    if (count >= cap) {
      size_t new_cap = cap * 2;
      ps_proc_info_t *new_procs = (ps_proc_info_t *)realloc(procs, new_cap * sizeof(ps_proc_info_t));
      if (!new_procs) {
        break;
      }
      procs = new_procs;
      cap = new_cap;
    }

    if (parse_proc_status(pid, &procs[count])) {
      count++;
    }
  }
  closedir(d);

  if (count > 1) {
    qsort(procs, count, sizeof(ps_proc_info_t), compare_procs);
  }

  char time_str[16];
  char stime_str[16];

  if (full_format) {
    printf("%-5s %5s %5s %2s %-5s %-8s %8s %s\n",
           "UID", "PID", "PPID", "C", "STIME", "TTY", "TIME", "CMD");
    for (size_t i = 0; i < count; i++) {
      print_time_formatted(time_str, sizeof(time_str), procs[i].cpu_time_us);
      print_stime_formatted(stime_str, sizeof(stime_str), procs[i].start_time_us);
      printf("%-5s %5d %5d %2d %-5s %-8s %8s %s\n",
             "root",
             procs[i].pid,
             procs[i].ppid,
             0,
             stime_str,
             procs[i].tty,
             time_str,
             procs[i].cmdline);
    }
  } else if (long_format) {
    printf("%1s %-5s %5s %5s %2s %-8s %8s %s\n",
           "S", "UID", "PID", "PPID", "C", "TTY", "TIME", "CMD");
    for (size_t i = 0; i < count; i++) {
      print_time_formatted(time_str, sizeof(time_str), procs[i].cpu_time_us);
      printf("%c %-5s %5d %5d %2d %-8s %8s %s\n",
             procs[i].state_c,
             "root",
             procs[i].pid,
             procs[i].ppid,
             0,
             procs[i].tty,
             time_str,
             procs[i].cmdline);
    }
  } else {
    printf("%5s %-8s %8s %s\n", "PID", "TTY", "TIME", "CMD");
    for (size_t i = 0; i < count; i++) {
      print_time_formatted(time_str, sizeof(time_str), procs[i].cpu_time_us);
      printf("%5d %-8s %8s %s\n",
             procs[i].pid,
             procs[i].tty,
             time_str,
             procs[i].cmdline);
    }
  }

  free(procs);
  return 0;
}

EOS_BIN_ATTR eos_bin_t ps_app = {
    EOS_BIN_INITIALIZER,
    .filename = "ps",
    .name = "Process Status",
    .group = "Utils",
    .description = "Report current process status snapshot from /proc",
    .entry_point = ps_main,
};
