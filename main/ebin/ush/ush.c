#include "ecore/bin.h"
#include "ecore/log.h"
#include "ecore/proc.h"
#include "ebin/ush/ush.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"
#include <ctype.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int ush_readline(char *buf, size_t size, bool force_tty) {
  size_t len = 0;

  for (;;) {
    eos_proc_t *proc = eos_proc_current();
    bool is_console = (!proc || eos_proc_fd_map_get(proc, STDIN_FILENO) < 0);
    bool is_tty = is_console || force_tty;

    // When booted without UART / USB-Serial-JTAG connected, wait calmly
    // instead of spinning on read() or returning -1 (which would exit ush
    // and cause eos_main to respawn init.sh / launcher in a loop).
    if (is_console && !eos_console_is_connected()) {
      usleep(5 * (useconds_t)EOS_DEFAULT_POLL_TIMEOUT_US);
      continue;
    }

    char ch = 0;
    ssize_t n = read(STDIN_FILENO, &ch, 1);

    if (n == 1) {
      switch (ch) {
      case '\r':
      case '\n':
        putchar('\n');
        fflush(stdout);
        buf[len] = '\0';
        return (int)len;

      case '\b':
      case '\x7f': // DEL
        if (len > 0) {
          len--;
          printf("\b \b");
          fflush(stdout);
        }
        break;

      default:
        // Filter out non-printable noise bytes on floating UART lines
        if ((unsigned char)ch >= ' ' && (unsigned char)ch <= '~') {
          if (len + 1 < size) {
            buf[len++] = ch;
            putchar(ch);
            fflush(stdout);
          }
        }
        break;
      }

      continue;
    }

    if (n == 0) {
      if (is_tty) {
        clearerr(stdin);
        usleep(5 * (useconds_t)EOS_DEFAULT_POLL_TIMEOUT_US);
        continue;
      }
      return -1;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK || is_console) {
      clearerr(stdin);
      usleep(EOS_DEFAULT_POLL_TIMEOUT_US);
      continue;
    }

    perror("read");
    return -1;
  }
}

static FILE *fopen_path(const char *name) {
  if (!name) return NULL;
  char resolved[EOS_MID_STR_LEN];
  if (eos_resolve_path(name, resolved, sizeof(resolved)) == 0) {
    FILE *f = fopen(resolved, "r");
    if (f) return f;
  }
  return fopen(name, "r");
}

static int ush_run_script(int argc, char **argv) {
  const int max_pos = 10;
  char **saved_pos = (char **)calloc((size_t)(max_pos + 1), sizeof(char *));
  char key_buf[EOS_XSMALL_STR_LEN];
  if (saved_pos) {
    const char *old_argc = getenv("#");
    saved_pos[max_pos] = old_argc ? strdup(old_argc) : NULL;
    for (int i = 0; i < max_pos; i++) {
      snprintf(key_buf, sizeof(key_buf), "%d", i);
      const char *prev = getenv(key_buf);
      saved_pos[i] = prev ? strdup(prev) : NULL;
      if (i + 1 < argc) {
        setenv(key_buf, argv[i + 1], 1);
      } else {
        unsetenv(key_buf);
      }
    }
    char argc_str[EOS_XSMALL_STR_LEN];
    snprintf(argc_str, sizeof(argc_str), "%d", (argc > 2) ? (argc - 2) : 0);
    setenv("#", argc_str, 1);
  }

  FILE *f = fopen_path(argv[1]);
  int ret = -1;
  if (!f) {
    perror("fopen script");
  } else {
    char line[USH_LINE_MAX];
    ret = 0;
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
  }

  if (saved_pos) {
    for (int i = 0; i < max_pos; i++) {
      snprintf(key_buf, sizeof(key_buf), "%d", i);
      if (saved_pos[i]) {
        setenv(key_buf, saved_pos[i], 1);
        free(saved_pos[i]);
      } else {
        unsetenv(key_buf);
      }
    }
    if (saved_pos[max_pos]) {
      setenv("#", saved_pos[max_pos], 1);
      free(saved_pos[max_pos]);
    } else {
      unsetenv("#");
    }
    free(saved_pos);
  }
  return ret;
}

int ush_main(int argc, char **argv) {
  bool force_tty = false;
  if (argc > 1 && strcmp(argv[1], "-i") == 0) {
    force_tty = true;
  } else if (argc > 1) {
    return ush_run_script(argc, argv);
  }

  eos_proc_t *self_proc = eos_proc_current();
  if (self_proc &&
      eos_proc_fd_map_get(self_proc, STDOUT_FILENO) >= 0 &&
      eos_proc_fd_map_get(self_proc, STDERR_FILENO) < 0) {
    dup2(STDOUT_FILENO, STDERR_FILENO);
  }

  printf("\neos shell — type 'help' for available commands\n\n");
  size_t free_hwm = eos_thread_stack_high_water_mark();
  size_t used = (USH_STACK_SIZE > free_hwm) ? (USH_STACK_SIZE - free_hwm) : 0;
  EOS_LOGI("ush: stack initialized (stack_size=%u, free_watermark=%u, actual_used=%u)",
           (unsigned)USH_STACK_SIZE,
           (unsigned)free_hwm,
           (unsigned)used);

  char line[USH_LINE_MAX];
  char path[EOS_MID_STR_LEN];

  for (;;) {
    int wstatus = 0;
    while (waitpid(-1, &wstatus, WNOHANG) > 0) {}

    getcwd(path, sizeof(path));

    printf(USH_WELCOME "%s > ", path);
    fflush(stdout);

    if (ush_readline(line, sizeof(line), force_tty) < 0)
      break;

    if (!line[0])
      continue;

    if (force_tty && (strncmp(line, "~/", 2) == 0 || strncmp(line, "E (", 3) == 0 ||
                      strncmp(line, "W (", 3) == 0 || strncmp(line, "I (", 3) == 0 ||
                      strncmp(line, "exit code:", 10) == 0 ||
                      strncmp(line, "eos shell", 9) == 0)) {
      continue;
    }

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
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .stack_size = USH_STACK_SIZE,
    .filename = "ush",
    .name = "MicroShell",
    .group = "System",
    .description = "EOS interactive command line shell with job control and I/O redirection",
    .entry_point = ush_main,
};
