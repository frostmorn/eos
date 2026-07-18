#include "misc/fancymacro.h"
#include "tests/tests.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tree_walk(const char *path, int depth, bool *last_flags) {
  DIR *dir = opendir(path);
  if (!dir)
    return;

  // count entries
  int count = 0;
  while (readdir(dir) != NULL)
    count++;
  rewinddir(dir);

  // allocate exactly what we need
  char (*names)[NAME_MAX + 1] = malloc(count * (NAME_MAX + 1));
  uint8_t *types = malloc(count * sizeof(uint8_t));

  if (!names || !types) {
    free(names);
    free(types);
    closedir(dir);
    return;
  }

  int i = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && i < count) {
    strlcpy(names[i], entry->d_name, NAME_MAX + 1);
    types[i] = entry->d_type;
    i++;
  }
  closedir(dir);

  for (i = 0; i < count; i++) {
    bool is_last = (i == count - 1);

    for (int d = 0; d < depth; d++)
      printf(last_flags[d] ? "    " : "│   ");

    printf(is_last ? "└── " : "├── ");
    printf("%s\n", names[i]);

    if (types[i] == DT_DIR) {
      char child_path[PATH_MAX];
      snprintf(child_path, sizeof(child_path), "%s/%s", path, names[i]);

      bool *child_flags = malloc((depth + 1) * sizeof(bool));
      if (child_flags) {
        if (last_flags && depth > 0)
          memcpy(child_flags, last_flags, depth * sizeof(bool));
        child_flags[depth] = is_last;
        tree_walk(child_path, depth + 1, child_flags);
        free(child_flags);
      }
    }
  }

  free(names);
  free(types);
}

int tree_main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: tree <path>\n");
    return 1;
  }
  printf("%s\n", argv[1]);
  tree_walk(argv[1], 0, NULL);
  printf("\n");
  return 0;
}