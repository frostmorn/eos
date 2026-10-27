#include <stdio.h>
#include "ecore/bin.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>

// Simple utility designed to open file and write it's contents to stdout
#define CAT_BUFFER_SIZE 512 // Standard size of sdcard sector

int cat_main(int argc, char **argv){

  char buffer[CAT_BUFFER_SIZE];
  if (argc < 2){
    ssize_t count_read = 0;
    while ((count_read = read(STDIN_FILENO, buffer, CAT_BUFFER_SIZE)) > 0){
      fwrite(buffer, 1, (size_t)count_read, stdout);
      usleep(10 * 1000);
    }
    fflush(stdout);
    return (count_read < 0) ? -1 : 0;
  }

  int ret = 0;
  for (int i = 1; i < argc; i++) {
    const char *path = argv[i];

    if (strcmp(path, "-") == 0) {
      ssize_t count_read = 0;
      while ((count_read = read(STDIN_FILENO, buffer, CAT_BUFFER_SIZE)) > 0) {
        fwrite(buffer, 1, (size_t)count_read, stdout);
        usleep(10 * 1000);
      }
      continue;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
      printf("Can't open file %s. %d: %s\n", path, errno, strerror(errno));
      ret = -1;
      continue;
    }

    size_t count_read = 0;
    while ((count_read = fread(buffer, 1, CAT_BUFFER_SIZE, fp)) > 0) {
      fwrite(buffer, 1, count_read, stdout);
      usleep(10 * 1000);
    }

    fclose(fp);
  }

  fflush(stdout);
  return ret;
}


EOS_BIN_ATTR eos_bin_t cat_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "cat",
    .name = "Concatenate File",
    .group = "Utils",
    .description = "Concatenate and print file contents or standard input to stdout",
    .entry_point = cat_main};
