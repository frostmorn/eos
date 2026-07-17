#include "misc/fancymacro.h"
#include "tests/tests.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define TREE_MAX_ENTRIES 256
#define TREE_MAX_DEPTH 64

static void tree_walk(const char *path, int depth, bool *last_flags) {
  DIR *dir = opendir(path);
  if (!dir)
    return;

  char names[TREE_MAX_ENTRIES][NAME_MAX + 1];
  uint8_t types[TREE_MAX_ENTRIES];
  int count = 0;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && count < TREE_MAX_ENTRIES) {
    strlcpy(names[count], entry->d_name, NAME_MAX + 1);
    types[count] = entry->d_type;
    count++;
  }
  closedir(dir);

  for (int i = 0; i < count; i++) {
    bool is_last = (i == count - 1);

    for (int d = 0; d < depth; d++)
      printf(last_flags[d] ? "    " : "│   ");

    printf(is_last ? "└── " : "├── ");
    printf("%s\n", names[i]);

    if (types[i] == DT_DIR) {
      char child_path[PATH_MAX];
      snprintf(child_path, sizeof(child_path), "%s/%s", path, names[i]);
      last_flags[depth] = is_last;
      tree_walk(child_path, depth + 1, last_flags);
    }
  }
}

int tree_main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: tree <path>\n");
    return 1;
  }
  const char *path = argv[1];
  printf("%s\n", path);
  bool last_flags[TREE_MAX_DEPTH] = {0};
  tree_walk(path, 0, last_flags);
  printf("\n");
  return 0;
}