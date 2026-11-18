#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "ecore/bin.h"
#include "ecore/input.h"
#include "ecore/ioctl.h"
#include "ebin/test_input/test_input.h"

static const char *get_key_name(uint16_t code) {
  switch (code) {
    case KEY_UP: return "KEY_UP";
    case KEY_DOWN: return "KEY_DOWN";
    case KEY_LEFT: return "KEY_LEFT";
    case KEY_RIGHT: return "KEY_RIGHT";
    case KEY_ENTER: return "KEY_ENTER (select)";
    case KEY_SPACE: return "KEY_SPACE (start)";
    case KEY_A: return "KEY_A";
    case KEY_B: return "KEY_B";
    case KEY_C: return "KEY_C";
    case KEY_D: return "KEY_D";
    case KEY_SLEEP: return "KEY_SLEEP";
    case KEY_ESC: return "KEY_ESC";
    default: return "UNKNOWN";
  }
}

static int poll_input_event(int fd, eos_input_event_t *ev, int timeout_ms) {
  int elapsed_ms = 0;
  while (timeout_ms < 0 || elapsed_ms < timeout_ms) {
    int ret = ioctl(fd, EOS_KEYBOARD_IOCTL_POLL, ev);
    if (ret > 0) {
      return 1;
    }
    if (ret < 0) {
      ssize_t n = read(fd, ev, sizeof(*ev));
      if (n == (ssize_t)sizeof(*ev)) {
        return 1;
      }
      return -1;
    }
    usleep(EOS_DEFAULT_POLL_TIMEOUT_US);
    elapsed_ms += 10;
  }
  return 0; // Timeout
}

static void print_usage(void) {
  printf("Usage: test_input [options] [max_events] [/dev/input_path]\n\n");
  printf("Options:\n");
  printf("  -a, --auto            Run automated self-test (inject event, verify, exit)\n");
  printf("  -t, --timeout <ms>    Set polling timeout in milliseconds (default: 1000 ms)\n");
  printf("  -i, --inject <key>    Inject synthetic key (e.g. 'up', 'down', 'enter')\n");
  printf("  -f, --forever         Interactive mode without timeout (press ESC to exit)\n");
  printf("  -h, --help            Show this help message\n");
}

static int parse_input_args(int argc, char **argv, test_input_opts_t *opts) {
  opts->path = "/dev/keyboard0";
  opts->max_events = 0;
  opts->timeout_ms = EOS_MS_PER_SEC;
  opts->auto_mode = false;
  opts->forever_mode = false;
  opts->inject_key = NULL;

  if (argc == 1) {
    opts->auto_mode = true;
    opts->max_events = 1;
    return 0;
  }

  int pos_idx = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 1;
    }
    if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto") == 0) {
      opts->auto_mode = true;
      if (opts->max_events == 0) opts->max_events = 1;
    } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) && i + 1 < argc) {
      opts->timeout_ms = atoi(argv[++i]);
    } else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--inject") == 0) && i + 1 < argc) {
      opts->inject_key = argv[++i];
    } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--forever") == 0) {
      opts->forever_mode = true;
    } else {
      pos_idx++;
      if (pos_idx == 1 && argv[i][0] != '/') {
        opts->max_events = atoi(argv[i]);
      } else if (argv[i][0] == '/') {
        opts->path = argv[i];
      }
    }
  }

  if (opts->forever_mode) {
    opts->timeout_ms = -1;
  } else if (opts->max_events == 0 && !opts->auto_mode) {
    opts->max_events = 1;
  }
  return 0;
}

static void test_input_auto_inject(int fd, bool *auto_verified, int *count) {
  printf("Running automated input self-test (injecting KEY_UP)...\n");
  for (int attempt = 0; attempt < 10 && !*auto_verified; attempt++) {
    if (write(fd, "up\n", 3) < 0) {
      printf("Warning: failed to inject key: %s\n", strerror(errno));
    }
    eos_input_event_t ev;
    memset(&ev, 0, sizeof(ev));
    int res = ioctl(fd, EOS_KEYBOARD_IOCTL_POLL, &ev);
    if (res > 0) {
      if (ev.type == EOS_INPUT_EVENT_KEYBOARD) {
        (*count)++;
        printf("[%d] Event: KEYBOARD, code=%u (%s)\n", *count, ev.code, get_key_name(ev.code));
        if (ev.code == KEY_UP) {
          *auto_verified = true;
          break;
        }
      }
    }
    usleep(TEST_INPUT_RETRY_INTERVAL_US);
  }
}

static int poll_and_print_events(int fd, const test_input_opts_t *opts, bool *auto_verified, int *count) {
  while ((opts->max_events == 0 || *count < opts->max_events) && (!opts->auto_mode || !*auto_verified)) {
    eos_input_event_t ev;
    memset(&ev, 0, sizeof(ev));
    int res = poll_input_event(fd, &ev, opts->timeout_ms);
    if (res < 0) {
      printf("Read error: %d (%s)\n", errno, strerror(errno));
      return -1;
    }
    if (res == 0) {
      if (!opts->auto_mode) {
        printf("Timeout (%d ms) reached, no events received.\n", opts->timeout_ms);
      }
      break;
    }

    (*count)++;
    if (ev.type == EOS_INPUT_EVENT_KEYBOARD) {
      printf("[%d] Event: KEYBOARD, code=%u (%s)\n", *count, ev.code, get_key_name(ev.code));
      if (opts->auto_mode && ev.code == KEY_UP) {
        *auto_verified = true;
      }
      if (ev.code == KEY_ESC) {
        printf("ESC pressed, exiting.\n");
        break;
      }
    } else if (ev.type == EOS_INPUT_EVENT_TOUCH) {
      printf("[%d] Event: TOUCH, x=%u, y=%u, pressure=%u, state=%u\n", *count,
             ev.x, ev.y, ev.pressure, ev.state);
    } else {
      printf("[%d] Event: UNKNOWN type=%u, code=%u\n", *count, ev.type, ev.code);
    }
  }
  return 0;
}

int test_input_main(int argc, char **argv) {
  test_input_opts_t opts;
  if (parse_input_args(argc, argv, &opts) != 0) {
    return 0;
  }

  printf("Opening input device: %s...\n", opts.path);
  int open_flags = (opts.auto_mode || opts.inject_key != NULL) ? O_RDWR : O_RDONLY;
  int fd = open(opts.path, open_flags);
  if (fd < 0 && open_flags == O_RDWR) {
    fd = open(opts.path, O_RDONLY);
  }
  if (fd < 0) {
    printf("Failed to open %s: %d (%s)\n", opts.path, errno, strerror(errno));
    return -1;
  }

  int count = 0;
  bool auto_verified = false;

  if (opts.auto_mode) {
    test_input_auto_inject(fd, &auto_verified, &count);
  } else if (opts.inject_key != NULL) {
    printf("Injecting synthetic key '%s'...\n", opts.inject_key);
    char inj_buf[EOS_LINE_64_LEN];
    int inj_len = snprintf(inj_buf, sizeof(inj_buf), "%s\n", opts.inject_key);
    if (write(fd, inj_buf, inj_len) < 0) {
      printf("Warning: failed to inject key: %s\n", strerror(errno));
    }
  } else {
    if (opts.timeout_ms < 0) {
      printf("Listening for input events (Press ESC to exit, max %d events)...\n", opts.max_events);
    } else {
      printf("Listening for input events (max %d events, timeout %d ms)...\n", opts.max_events, opts.timeout_ms);
    }
  }

  if (poll_and_print_events(fd, &opts, &auto_verified, &count) < 0) {
    close(fd);
    return -1;
  }

  close(fd);

  if (opts.auto_mode) {
    if (auto_verified) {
      printf("Automated input self-test PASSED.\n");
    } else {
      printf("Automated input self-test FAILED: expected KEY_UP event.\n");
      return -1;
    }
  }

  printf("test_input finished (%d events received).\n", count);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_input = {
    EOS_BIN_INITIALIZER,
    .filename = "test_input",
    .name = "Input Test",
    .group = "Tests",
    .description = "Automated and interactive input event monitor for /dev/keyboard0",
    .entry_point = test_input_main};
