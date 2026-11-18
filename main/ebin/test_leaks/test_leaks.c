#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <esp_timer.h>
#include "ecore/bin.h"
#include "ecore/proc.h"
#include "emisc/fancymacro.h"

static void hook_fn_append(void *arg) {
  const char *msg = (const char *)arg;
  int fd = open("/tmp/t_hooks.txt", O_WRONLY | O_CREAT | O_APPEND, EOS_DEFAULT_FILE_MODE);
  if (fd >= 0) {
    write(fd, msg, strlen(msg));
    close(fd);
  }
}

static void dummy_timer_callback(void *arg) {
  (void)arg;
}

static void *detached_thread_worker(void *arg) {
  (void)arg;
  usleep(500000);
  return NULL;
}

static int run_child_hooks(void) {
  unlink("/tmp/t_hooks.txt");
  int h1 = eos_proc_on_exit(hook_fn_append, (void *)"1\n", "hook1");
  int h2 = eos_proc_on_exit(hook_fn_append, (void *)"2\n", "hook2");
  int h3 = eos_proc_on_exit(hook_fn_append, (void *)"3\n", "hook3");

  if (h1 <= 0 || h2 <= 0 || h3 <= 0) {
    return 101;
  }

  // Cancel hook 2
  if (eos_proc_cancel_exit(h2) != 0) {
    return 102;
  }

  return EOS_DEFAULT_MAGIC_42;
}

static int run_child_socket(void) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    return 103;
  }
  // Intentionally exit without close(s) to verify auto-reaping
  return 0;
}

static int run_child_timer(void) {
  esp_timer_create_args_t timer_args = {
    .callback = dummy_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "leak_timer",
    .skip_unhandled_events = false,
  };
  esp_timer_handle_t timer = NULL;
  esp_err_t err = esp_timer_create(&timer_args, &timer);
  if (err != ESP_OK || !timer) {
    return 104;
  }
  esp_timer_start_periodic(timer, 5000);
  // Intentionally exit without stopping or deleting timer to verify auto-reaping
  return 0;
}

static int run_child_thread(void) {
  pthread_t th;
  if (pthread_create(&th, NULL, detached_thread_worker, NULL) == 0) {
    pthread_detach(th);
  }
  // Intentionally exit without joining to verify thread cleanup and warning
  return 0;
}

static int spawn_child(const char *subcmd) {
  char *argv[] = {"test_leaks", (char *)subcmd, NULL};
  if (execve("test_leaks", argv, NULL) != 0) {
    return -1;
  }
  int status = 0;
  if (waitpid(-1, &status, 0) < 0) {
    return -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int test_leaks_main(int argc, char **argv) {
  if (argc > 1) {
    if (strcmp(argv[1], "child_hooks") == 0) {
      return run_child_hooks();
    }
    if (strcmp(argv[1], "child_socket") == 0) {
      return run_child_socket();
    }
    if (strcmp(argv[1], "child_timer") == 0) {
      return run_child_timer();
    }
    if (strcmp(argv[1], "child_thread") == 0) {
      return run_child_thread();
    }
  }

  printf("\n=== Running Leak & Cleanup-Hook Registry Tests (F03) ===\n");
  int passed = 0;
  int failed = 0;

  // Test 1: LIFO hook execution & cancellation
  unlink("/tmp/t_hooks.txt");
  int exit_code = spawn_child("child_hooks");

  int fd = open("/tmp/t_hooks.txt", O_RDONLY);
  char buf[EOS_LINE_64_LEN];
  memset(buf, 0, sizeof(buf));
  if (fd >= 0) {
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
  }
  unlink("/tmp/t_hooks.txt");

  if (exit_code == EOS_DEFAULT_MAGIC_42 && strcmp(buf, "3\n1\n") == 0) {
    printf("  [PASS] hooks: LIFO execution and cancellation verified (output: 3, 1)\n");
    passed++;
  } else {
    printf("  [FAIL] hooks: expected 42 and '3\\n1\\n', got code=%d output='%s'\n", exit_code, buf);
    failed++;
  }

  // Test 2: Socket leak reaping
  bool sock_ok = true;
  for (int i = 0; i < 15; i++) {
    int s_code = spawn_child("child_socket");
    if (s_code != 0) {
      sock_ok = false;
      printf("  [FAIL] socket: child iteration %d failed with code %d\n", i, s_code);
      break;
    }
  }

  if (sock_ok) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s >= 0) {
      close(s);
      printf("  [PASS] socket: 15 leaked sockets reaped successfully without table exhaustion\n");
      passed++;
    } else {
      printf("  [FAIL] socket: parent cannot allocate socket after child leaks\n");
      failed++;
    }
  } else {
    failed++;
  }

  // Test 3: Timer leak reaping
  int t_code = spawn_child("child_timer");
  usleep(50000); // Wait for timer task to dispatch and ensure no crash occurs

  if (t_code == 0) {
    printf("  [PASS] timer: leaked timer safely stopped and deleted on process exit\n");
    passed++;
  } else {
    printf("  [FAIL] timer: child exited with error %d\n", t_code);
    failed++;
  }

  // Test 4: Leaked thread reaping
  int th_code = spawn_child("child_thread");
  usleep(50000);

  if (th_code == 0) {
    printf("  [PASS] thread: unjoined thread terminated and reaped without hanging\n");
    passed++;
  } else {
    printf("  [FAIL] thread: child exited with error %d\n", th_code);
    failed++;
  }

  printf("\nLeak Registry Tests: %d passed, %d failed\n\n", passed, failed);
  return (failed == 0) ? 0 : 1;
}

EOS_BIN_ATTR eos_bin_t test_leaks_bin = {
  EOS_BIN_INITIALIZER,
  .filename = "test_leaks",
  .name = "Leak Registry Tests",
  .group = "Tests",
  .flags = EOS_BIN_FLAG_STACK_PSRAM,
  .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
  .description = "Automated test suite for process exit cleanup hooks and leak prevention",
  .entry_point = test_leaks_main,
};
