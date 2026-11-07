#include "ecore/bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int uptime_main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: uptime [options]\n\n");
      printf("Tell how long the system has been running.\n\n");
      printf("Options:\n");
      printf("  -h, --help   display this help and exit\n");
      return 0;
    }
  }

  FILE *f = fopen("/proc/uptime", "r");
  double up_sec = 0.0;
  if (f) {
    if (fscanf(f, "%lf", &up_sec) != 1) {
      up_sec = 0.0;
    }
    fclose(f);
  }

  char load_str[64] = "0.00, 0.00, 0.00";
  f = fopen("/proc/loadavg", "r");
  if (f) {
    double l1 = 0, l5 = 0, l15 = 0;
    if (fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) == 3) {
      snprintf(load_str, sizeof(load_str), "%.2f, %.2f, %.2f", l1, l5, l15);
    }
    fclose(f);
  }

  unsigned long sec = (unsigned long)up_sec;
  unsigned int days = (unsigned int)(sec / 86400);
  unsigned int hours = (unsigned int)((sec % 86400) / 3600);
  unsigned int mins = (unsigned int)((sec % 3600) / 60);

  if (days > 0) {
    printf("up %u day%s, %02u:%02u, load average: %s\n",
           days, days == 1 ? "" : "s", hours, mins, load_str);
  } else if (hours > 0) {
    printf("up %u:%02u, load average: %s\n", hours, mins, load_str);
  } else {
    printf("up %u min, load average: %s\n", mins, load_str);
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t uptime_app = {
    EOS_BIN_INITIALIZER,
    .filename = "uptime",
    .name = "System Uptime",
    .group = "Utils",
    .description = "Tell how long the system has been running and load averages",
    .entry_point = uptime_main,
};
