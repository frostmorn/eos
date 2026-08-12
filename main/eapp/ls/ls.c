#include "ecore/app.h"
#include <dirent.h>
#include <stdio.h>


int ls_main(int argc, char **argv){
  if (argc < 2) {
    printf("usage: ls <path>\n");
    return -1;
  }
  
  const char *path = argv[1];

  DIR *dir = opendir(path);

  if (!dir){
    printf("Can't open dir %s\n", path);
    return -1;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL){
    printf("%s\n", entry->d_name);
  }
  
  closedir(dir);
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t ls_app = {
    EOS_NATIVE_APP_INIT, .filename = "ls", .name = "ls",
    .entry_point = ls_main};
