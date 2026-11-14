#include "ecore/bin.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_usage(void) {
  printf("Usage: ln [-s] [-f] [-v] TARGET [LINKNAME]\n"
         "Create a link to TARGET with the name LINKNAME.\n\n"
         "Options:\n"
         "  -s, --symbolic       Make symbolic links instead of hard links\n"
         "  -f, --force          Remove existing destination files\n"
         "  -v, --verbose        Print name of each linked file\n"
         "  -h, --help           Display this help and exit\n");
}

static const char *get_basename(const char *path) {
  if (!path || !path[0]) return "";
  const char *p = path + strlen(path) - 1;
  while (p > path && *p == '/') p--;
  while (p > path && *(p - 1) != '/') p--;
  return p;
}

int ln_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return -1;
  }

  bool symbolic = false;
  bool force = false;
  bool verbose = false;
  const char *target = NULL;
  const char *linkname = NULL;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && argv[i][1] != '\0') {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
        print_usage();
        return 0;
      }
      if (strcmp(argv[i], "--symbolic") == 0 || strcmp(argv[i], "-s") == 0) {
        symbolic = true;
        continue;
      }
      if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
        force = true;
        continue;
      }
      if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
        verbose = true;
        continue;
      }
      if (argv[i][1] != '-') {
        // Multi-flag support like -sf
        for (size_t c = 1; c < strlen(argv[i]); c++) {
          if (argv[i][c] == 's') symbolic = true;
          else if (argv[i][c] == 'f') force = true;
          else if (argv[i][c] == 'v') verbose = true;
          else if (argv[i][c] == 'h') {
            print_usage();
            return 0;
          } else {
            printf("ln: invalid option -- '%c'\n", argv[i][c]);
            return -1;
          }
        }
        continue;
      }
      printf("ln: unrecognized option '%s'\n", argv[i]);
      return -1;
    }

    if (!target) {
      target = argv[i];
    } else if (!linkname) {
      linkname = argv[i];
    } else {
      printf("ln: target '%s' is not a directory\n", argv[i]);
      return -1;
    }
  }

  if (!target) {
    print_usage();
    return -1;
  }

  char dst_buf[PATH_MAX];
  if (!linkname) {
    linkname = get_basename(target);
  } else {
    struct stat st_dst;
    if (stat(linkname, &st_dst) == 0 && S_ISDIR(st_dst.st_mode)) {
      const char *bname = get_basename(target);
      size_t len = strlen(linkname);
      if (len > 0 && linkname[len - 1] == '/') {
        snprintf(dst_buf, sizeof(dst_buf), "%s%s", linkname, bname);
      } else {
        snprintf(dst_buf, sizeof(dst_buf), "%s/%s", linkname, bname);
      }
      linkname = dst_buf;
    }
  }

  if (force) {
    unlink(linkname);
  }

  int ret;
  if (symbolic) {
    ret = symlink(target, linkname);
  } else {
    ret = link(target, linkname);
  }

  if (ret != 0) {
    printf("ln: failed to create %s link '%s' -> '%s': %s\n",
           symbolic ? "symbolic" : "hard", linkname, target, strerror(errno));
    return -1;
  }

  if (verbose) {
    printf("'%s' -> '%s'\n", linkname, target);
  }

  return 0;
}

EOS_BIN_ATTR eos_bin_t ln_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "ln",
    .name = "Make Links",
    .group = "Utils",
    .description = "Make links between files",
    .entry_point = ln_main,
};
