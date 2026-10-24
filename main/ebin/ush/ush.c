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

static bool has_redirection_or_pipe(const char *line) {
  bool in_squote = false;
  bool in_dquote = false;
  for (const char *p = line; *p; p++) {
    if (*p == '\\' && p[1]) {
      p++;
      continue;
    }
    if (*p == '\'' && !in_dquote) {
      in_squote = !in_squote;
      continue;
    }
    if (*p == '"' && !in_squote) {
      in_dquote = !in_dquote;
      continue;
    }
    if (!in_squote && !in_dquote) {
      if (*p == '|' || *p == '<' || *p == '>')
        return true;
    }
  }
  return false;
}

static char *skip_ws(char *p) {
  while (*p && isspace((unsigned char)*p))
    p++;
  return *p ? p : NULL;
}

static char *token_end(char *p) {
  while (*p && !isspace((unsigned char)*p))
    p++;
  return p;
}

int ush_main(int argc, char **argv) {
  printf("\neos shell — type 'help' for available commands\n\n");

  char line[USH_LINE_MAX];
  char path[PATH_MAX];

  for (;;) {
    getcwd(path, PATH_MAX);

    printf(USH_WELCOME "%s > ", path);
    // Why it's not flushed? 
    fflush(stdout);

    if (ush_readline(line, sizeof(line)) < 0)
      break;

    if (!line[0])
      continue;

    if (!strcmp(line, "exit"))
      break;

    if (!strcmp(line, "help")) {
      // Execute tree via execv
      char *help_argv[] = {"tree", "/bin", NULL};
      if (execv("tree", help_argv) == 0) {
        int status = 0;
        eos_wait(&status);
      }
      continue;
    }

    fflush(stdout);

    if (has_redirection_or_pipe(line)) {
      int ret = system(line);
      if (ret)
        printf("exit code: %d\n", ret);
      continue;
    }

    char *buf = strdup(line);
    int ret = 0;
    if (buf) {
      int c_argc = 0;
      char *p = skip_ws(buf);
      while (p) {
        c_argc++;
        char *end = token_end(p);
        p = *end ? skip_ws(end) : NULL;
      }

      if (c_argc > 0) {
        char **c_argv = calloc(c_argc + 1, sizeof(*c_argv));
        if (c_argv) {
          c_argc = 0;
          p = skip_ws(buf);
          while (p) {
            char *end = token_end(p);
            c_argv[c_argc++] = p;
            if (*end) {
              *end = '\0';
              p = skip_ws(end + 1);
            } else
              p = NULL;
          }
          c_argv[c_argc] = NULL;

          if (execv(c_argv[0], c_argv) == 0) {
            int status = 0;
            if (eos_wait(&status) == 0) {
              ret = status;
            } else {
              ret = -1;
            }
          } else {
            perror("execv");
            ret = -1;
          }
          free(c_argv);
        } else {
          ret = -1;
        }
      }
      free(buf);
    } else {
      ret = -1;
    }

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
