#include "ecore/bin.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_usage(void) {
  printf("Usage: readlink [-f] [-n] FILE...\n"
         "Print value of a symbolic link or canonical file name.\n\n"
         "Options:\n"
         "  -f, --canonicalize   Canonicalize by following every symlink\n"
         "  -n, --no-newline     Do not output the trailing newline\n"
         "  -h, --help           Display this help and exit\n");
}

int readlink_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return -1;
  }

  bool canonicalize = false;
  bool no_newline = false;
  int first_arg = 0;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && argv[i][1] != '\0') {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        print_usage();
        return 0;
      }
      if (strcmp(argv[i], "--canonicalize") == 0 || strcmp(argv[i], "-f") == 0) {
        canonicalize = true;
        continue;
      }
      if (strcmp(argv[i], "--no-newline") == 0 || strcmp(argv[i], "-n") == 0) {
        no_newline = true;
        continue;
      }
      if (argv[i][1] != '-') {
        for (size_t c = 1; c < strlen(argv[i]); c++) {
          if (argv[i][c] == 'f') canonicalize = true;
          else if (argv[i][c] == 'n') no_newline = true;
          else if (argv[i][c] == 'h') {
            print_usage();
            return 0;
          } else {
            printf("readlink: invalid option -- '%c'\n", argv[i][c]);
            return -1;
          }
        }
        continue;
      }
      printf("readlink: unrecognized option '%s'\n", argv[i]);
      return -1;
    }

    first_arg = i;
    break;
  }

  if (first_arg == 0) {
    print_usage();
    return -1;
  }

  int ret = 0;
  char buf[PATH_MAX];

  for (int i = first_arg; i < argc; i++) {
    const char *target = argv[i];
    if (canonicalize) {
      if (realpath(target, buf) != NULL) {
        printf("%s%s", buf, no_newline ? "" : "\n");
      } else {
        ret = -1;
      }
    } else {
      ssize_t len = readlink(target, buf, sizeof(buf) - 1);
      if (len >= 0) {
        buf[len] = '\0';
        printf("%s%s", buf, no_newline ? "" : "\n");
      } else {
        ret = -1;
      }
    }
  }

  return ret;
}

EOS_BIN_ATTR eos_bin_t readlink_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "readlink",
    .name = "Readlink",
    .group = "Utils",
    .description = "Print value of a symbolic link",
    .entry_point = readlink_main,
};
