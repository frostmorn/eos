#include "ecore/bin.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

int write_main(int argc, char **argv) {

  if (argc != 3) {
    printf("Usage: write <value> <file>\n");
    return -1;
  }

  FILE *f = fopen(argv[2], "w");
  if (!f) {
    printf("write: %d:%s\n", errno, strerror(errno));
    return -1;
  }

  size_t len = strlen(argv[1]);

  if (fwrite(argv[1], 1, len, f) != len) {
    printf("write: %d:%s\n", errno, strerror(errno));
    fclose(f);
    return -1;
  }

  fclose(f);

  return 0;
}

EOS_BIN_ATTR eos_bin_t write_app = {EOS_BIN_INITIALIZER, .filename = "write",
                                    .name = "write", .entry_point = write_main};