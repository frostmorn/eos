#include "ecore/bin.h"
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int ls_main(int argc, char **argv){ 
  if (argc < 2) {
    char *path = getcwd(NULL, 0);
    DIR *dir = opendir(path ? path : ".");
    if (!dir) {
      printf("Can't open dir %s\n", path ? path : ".");
      if (path) free(path);
      return -1;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      printf("%s\n", entry->d_name);
    }
    closedir(dir);
    if (path) free(path);
    return 0;
  }

  int ret = 0;
  for (int i = 1; i < argc; i++) {
    const char *path = argv[i];
    DIR *dir = opendir(path);
    if (dir) {
      if (argc > 2) {
        printf("%s:\n", path);
      }
      struct dirent *entry;
      while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
      }
      closedir(dir);
      if (argc > 2 && i < argc - 1) {
        putchar('\n');
      }
    } else {
      FILE *f = fopen(path, "r");
      if (f) {
        fclose(f);
        printf("%s\n", path);
      } else {
        printf("Can't open dir %s\n", path);
        ret = -1;
      }
    }
  }

  return ret;
}

EOS_BIN_ATTR eos_bin_t ls_app = {
    EOS_BIN_INITIALIZER,
    .filename = "ls",
    .name = "List Directory",
    .group = "Utils",
    .description = "List files and subdirectories in specified or current working directory",
    .entry_point = ls_main};
