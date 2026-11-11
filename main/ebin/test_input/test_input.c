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
      // Driver does not implement polling ioctl; fall back to read
      ssize_t n = read(fd, ev, sizeof(*ev));
      if (n == (ssize_t)sizeof(*ev)) {
        return 1;
      }
      return -1;
    }
    usleep(10000); // 10 ms
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

int test_input_main(int argc, char **argv) {
  const char *path = "/dev/keyboard0";
  int max_events = 0;
  int timeout_ms = 1000;
  bool auto_mode = false;
  bool forever_mode = false;
  const char *inject_key = NULL;

  if (argc == 1) {
    // Default with no arguments: automated headless self-test
    auto_mode = true;
    max_events = 1;
  } else {
    int pos_idx = 0;
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
        print_usage();
        return 0;
      }
      if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto") == 0) {
        auto_mode = true;
        if (max_events == 0) max_events = 1;
        continue;
      }
      if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--forever") == 0) {
        forever_mode = true;
        continue;
      }
      if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) {
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          timeout_ms = atoi(argv[++i]);
        }
        continue;
      }
      if (strncmp(argv[i], "--timeout=", 10) == 0) {
        timeout_ms = atoi(argv[i] + 10);
        continue;
      }
      if (strncmp(argv[i], "-t=", 3) == 0) {
        timeout_ms = atoi(argv[i] + 3);
        continue;
      }
      if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--inject") == 0) {
        if (i + 1 < argc && argv[i + 1][0] != '-') {
          inject_key = argv[++i];
        }
        continue;
      }
      if (strncmp(argv[i], "--inject=", 9) == 0) {
        inject_key = argv[i] + 9;
        continue;
      }
      if (strncmp(argv[i], "-i=", 3) == 0) {
        inject_key = argv[i] + 3;
        continue;
      }

      // Positional arguments
      pos_idx++;
      if (pos_idx == 1 && argv[i][0] != '/') {
        max_events = atoi(argv[i]);
      } else if (argv[i][0] == '/') {
        path = argv[i];
      }
    }
  }

  if (forever_mode) {
    timeout_ms = -1;
  } else if (max_events == 0 && !auto_mode) {
    max_events = 1;
  }

  printf("Opening input device: %s...\n", path);
  int open_flags = (auto_mode || inject_key != NULL) ? O_RDWR : O_RDONLY;
  int fd = open(path, open_flags);
  if (fd < 0 && open_flags == O_RDWR) {
    // Fall back to read-only if write access is unavailable
    fd = open(path, O_RDONLY);
  }
  if (fd < 0) {
    printf("Failed to open %s: %d (%s)\n", path, errno, strerror(errno));
    return -1;
  }

  int count = 0;
  bool auto_verified = false;
  eos_input_event_t ev;
  memset(&ev, 0, sizeof(ev));

  // In auto mode, perform atomic injection and dequeue to prevent background GUI tasks from stealing events
  if (auto_mode) {
    printf("Running automated input self-test (injecting KEY_UP)...\n");
    if (write(fd, "up\n", 3) < 0) {
      printf("Warning: failed to inject key: %s\n", strerror(errno));
    }
    // Dequeue immediately before background tasks can steal the event
    int res = ioctl(fd, EOS_KEYBOARD_IOCTL_POLL, &ev);
    if (res > 0) {
      count = 1;
      if (ev.type == EOS_INPUT_EVENT_KEYBOARD) {
        printf("[%d] Event: KEYBOARD, code=%u (%s)\n", count, ev.code, get_key_name(ev.code));
        if (ev.code == KEY_UP) {
          auto_verified = true;
        }
      }
    }
  } else if (inject_key != NULL) {
    printf("Injecting synthetic key '%s'...\n", inject_key);
    char inj_buf[64];
    int inj_len = snprintf(inj_buf, sizeof(inj_buf), "%s\n", inject_key);
    if (write(fd, inj_buf, inj_len) < 0) {
      printf("Warning: failed to inject key: %s\n", strerror(errno));
    }
  } else {
    if (timeout_ms < 0) {
      printf("Listening for input events (Press ESC to exit, max %d events)...\n", max_events);
    } else {
      printf("Listening for input events (max %d events, timeout %d ms)...\n", max_events, timeout_ms);
    }
  }

  // If not already satisfied by immediate dequeue, poll for remaining events
  while ((max_events == 0 || count < max_events) && (!auto_mode || !auto_verified)) {
    memset(&ev, 0, sizeof(ev));
    int res = poll_input_event(fd, &ev, timeout_ms);
    if (res < 0) {
      printf("Read error: %d (%s)\n", errno, strerror(errno));
      close(fd);
      return -1;
    }
    if (res == 0) {
      if (!auto_mode) {
        printf("Timeout (%d ms) reached, no events received.\n", timeout_ms);
      }
      break;
    }

    count++;
    if (ev.type == EOS_INPUT_EVENT_KEYBOARD) {
      printf("[%d] Event: KEYBOARD, code=%u (%s)\n", count, ev.code, get_key_name(ev.code));
      if (auto_mode && ev.code == KEY_UP) {
        auto_verified = true;
      }
      if (ev.code == KEY_ESC) {
        printf("ESC pressed, exiting.\n");
        break;
      }
    } else if (ev.type == EOS_INPUT_EVENT_TOUCH) {
      printf("[%d] Event: TOUCH, x=%u, y=%u, pressure=%u, state=%u\n", count,
             ev.x, ev.y, ev.pressure, ev.state);
    } else {
      printf("[%d] Event: UNKNOWN type=%u, code=%u\n", count, ev.type, ev.code);
    }
  }

  close(fd);

  if (auto_mode) {
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
