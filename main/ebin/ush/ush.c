#include "ecore/bin.h"
#include "emisc/fancymacro.h"
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

#define USH_LINE_MAX 256
#define USH_WELCOME EOS_MASCOT_R " "

static int ush_readline(char *buf, size_t size) {
  size_t len = 0;

  for (;;) {
    char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);

    if (n == 1) {
      switch (ch) {
      case '\r':
      case '\n':
        putchar('\n');
        buf[len] = '\0';
        return (int)len;

      case '\b':
      case 0x7f: // DEL
        if (len) {
          len--;
          printf("\b \b");
          fflush(stdout);
        }
        break;

      default:
        if (len < size - 1) {
          buf[len++] = ch;
          putchar(ch);
          fflush(stdout);
        }
        break;
      }

      continue;
    }

    if (n == 0) {
      // EOF
      return -1;
    }

    if (errno == EAGAIN) {
      clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    perror("read");
    return -1;
  }
}

static FILE *fopen_path(const char *name) {
  if (!name) return NULL;
  if (name[0] == '/' || strchr(name, '/')) {
    return fopen(name, "r");
  }

  char *path_env = getenv("PATH");
  if (!path_env || !path_env[0]) {
    path_env = "/bin";
  }

  char path_buf[PATH_MAX];
  strlcpy(path_buf, path_env, sizeof(path_buf));

  char *saveptr = NULL;
  char *dir = strtok_r(path_buf, ":", &saveptr);
  while (dir) {
    char candidate[PATH_MAX];
    snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
    FILE *f = fopen(candidate, "r");
    if (f) return f;
    dir = strtok_r(NULL, ":", &saveptr);
  }

  return fopen(name, "r");
}

int ush_main(int argc, char **argv) {
  if (argc > 1) {
    // Non-interactive script execution mode: read lines and pass to system()
    FILE *f = fopen_path(argv[1]);
    if (!f) {
      perror("fopen script");
      return -1;
    }
    char line[USH_LINE_MAX];
    int ret = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
      size_t len = strlen(line);
      while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
      }
      if (!line[0] || line[0] == '#')
        continue;

      ret = system(line);
    }
    fclose(f);
    return ret;
  }

  printf("\neos shell — type 'help' for available commands\n\n");

  char line[USH_LINE_MAX];
  char path[PATH_MAX];

  for (;;) {
    getcwd(path, PATH_MAX);

    printf(USH_WELCOME "%s > ", path);
    fflush(stdout);

    if (ush_readline(line, sizeof(line)) < 0)
      break;

    if (!line[0])
      continue;

    if (!strcmp(line, "exit"))
      break;

    if (!strcmp(line, "help")) {
      int ret = system("tree /bin");
      if (ret)
        printf("exit code: %d\n", ret);
      continue;
    }

    fflush(stdout);

    int ret = system(line);
    if (ret)
      printf("exit code: %d\n", ret);
  }
  return 0;
}

EOS_BIN_ATTR eos_bin_t ush = {
    EOS_BIN_INITIALIZER,     .filename = "ush",
    .name = "MicroShell",    .description = "EOS command line shell",
    .entry_point = ush_main,
};
