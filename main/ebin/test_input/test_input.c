#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "ecore/bin.h"
#include "ecore/input.h"

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

int test_input_main(int argc, char **argv) {
  const char *path = "/dev/keyboard0";
  int max_events = 0; // 0 = infinite until ESC or error

  if (argc > 1) {
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      printf("Usage: test_input [max_events] [/dev/input_path]\n");
      return 0;
    }
    max_events = atoi(argv[1]);
  }
  if (argc > 2) {
    path = argv[2];
  }

  printf("Opening input device: %s...\n", path);
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("Failed to open %s: %d (%s)\n", path, errno, strerror(errno));
    return -1;
  }

  printf("Listening for input events (Press ESC to exit, or max %d events)...\n", max_events);

  int count = 0;
  eos_input_event_t ev;

  while (max_events == 0 || count < max_events) {
    ssize_t n = read(fd, &ev, sizeof(ev));
    if (n < 0) {
      printf("Read error: %d (%s)\n", errno, strerror(errno));
      close(fd);
      return -1;
    }
    if ((size_t)n < sizeof(ev)) {
      continue;
    }

    count++;
    if (ev.type == EOS_INPUT_EVENT_KEYBOARD) {
      printf("[%d] Event: KEYBOARD, code=%u (%s)\n", count, ev.code, get_key_name(ev.code));
      if (ev.code == KEY_ESC) {
        printf("ESC pressed, exiting.\n");
        break;
      }
    } else if (ev.type == EOS_INPUT_EVENT_TOUCH) {
      printf("[%d] Event: TOUCH, x=%u, y=%u\n", count, ev.x, ev.y);
    } else {
      printf("[%d] Event: UNKNOWN type=%u, code=%u\n", count, ev.type, ev.code);
    }
  }

  close(fd);
  printf("test_input finished (%d events received).\n", count);
  return 0;
}

EOS_BIN_ATTR eos_bin_t test_input = {
    EOS_BIN_INITIALIZER, .filename = "test_input", .name = "test_input",
    .entry_point = test_input_main};
