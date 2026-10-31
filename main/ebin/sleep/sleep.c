#include "ecore/bin.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

static void print_usage(void) {
  printf("Usage: sleep NUMBER[s|ms|m|h]...\n"
         "Pause for NUMBER seconds, milliseconds, minutes, or hours.\n");
}

int sleep_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return -1;
  }

  double total_seconds = 0.0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 0;
    }

    char *endptr = NULL;
    errno = 0;
    double val = strtod(argv[i], &endptr);

    if (errno != 0 || endptr == argv[i] || val < 0.0) {
      printf("sleep: invalid time interval '%s'\n", argv[i]);
      return -1;
    }

    if (*endptr != '\0') {
      if (strcasecmp(endptr, "ms") == 0) {
        val /= 1000.0;
      } else if (strcasecmp(endptr, "s") == 0) {
        // seconds
      } else if (strcasecmp(endptr, "m") == 0) {
        val *= 60.0;
      } else if (strcasecmp(endptr, "h") == 0) {
        val *= 3600.0;
      } else if (strcasecmp(endptr, "d") == 0) {
        val *= 86400.0;
      } else {
        printf("sleep: invalid time interval '%s'\n", argv[i]);
        return -1;
      }
    }

    total_seconds += val;
  }

  uint64_t total_usec = (uint64_t)(total_seconds * 1000000.0);
  while (total_usec > 0) {
    useconds_t chunk = (total_usec > 500000ULL) ? 500000U : (useconds_t)total_usec;
    usleep(chunk);
    total_usec -= chunk;
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t sleep_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .stack_size = EOS_BIN_MIN_STACK_SIZE,
    .filename = "sleep",
    .name = "Sleep",
    .group = "Utils",
    .description = "Delay execution for a specified duration",
    .entry_point = sleep_main,
};
