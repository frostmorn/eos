#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "ecore/bin.h"

int echo_main(int argc, char **argv) {
  int start = 1;
  bool newline = true;

  if (argc > 1 && strcmp(argv[1], "-n") == 0) {
    newline = false;
    start = 2;
  }

  for (int i = start; i < argc; i++) {
    printf("%s", argv[i]);
    if (i < argc - 1) {
      putchar(' ');
    }
  }

  if (newline) {
    putchar('\n');
  }
  fflush(stdout);

  return 0;
}

EOS_BIN_ATTR eos_bin_t echo_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "echo",
    .name = "echo",
    .entry_point = echo_main
};
