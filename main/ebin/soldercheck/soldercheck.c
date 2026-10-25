#include "ecore/bin.h"
#include "ecore/ioctl.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

static const uint8_t s_pattern[] = {1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0};

typedef struct {
  char filename[PATH_MAX];
  int fd;
  int pin_num;
  bool faulty;
} pin_file_t;

static int compare_pins(const void *a, const void *b) {
  const pin_file_t *pa = (const pin_file_t *)a;
  const pin_file_t *pb = (const pin_file_t *)b;
  return pa->pin_num - pb->pin_num;
}

static void pin_reset_to_input_pullup(int fd) {
  ioctl(fd, EOS_GPIO_IOCTL_SET_PULLUP);
}

static void pin_reset_to_input_pulldown(int fd) {
  ioctl(fd, EOS_GPIO_IOCTL_SET_PULLDOWN);
}

static bool test_pin_bridge(int fd_drive, int fd_read) {
  // 1. Pull-up test: fd_read pulled up, fd_drive outputs 0.
  //    If bridged, fd_read is pulled down to 0.
  //    If not bridged, fd_read remains 1 due to pull-up.
  pin_reset_to_input_pullup(fd_read);
  uint8_t zero = 0;
  if (write(fd_drive, &zero, 1) < 0)
    return false;
  usleep(50);
  uint8_t val = 1;
  if (read(fd_read, &val, 1) < 0 || val != 0)
    return false;

  // 2. Pull-down test: fd_read pulled down, fd_drive outputs 1.
  //    If bridged, fd_read is pulled up to 1.
  //    If not bridged, fd_read remains 0 due to pull-down.
  pin_reset_to_input_pulldown(fd_read);
  uint8_t one = 1;
  if (write(fd_drive, &one, 1) < 0)
    return false;
  usleep(50);
  val = 0;
  if (read(fd_read, &val, 1) < 0 || val != 1)
    return false;

  // 3. Dynamic pattern verification under pull-up
  pin_reset_to_input_pullup(fd_read);
  for (size_t k = 0; k < sizeof(s_pattern); k++) {
    if (write(fd_drive, &s_pattern[k], 1) < 0)
      return false;
    usleep(30);
    val = 0;
    if (read(fd_read, &val, 1) < 0 || val != s_pattern[k])
      return false;
  }

  return true;
}

int soldercheck_main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("====================================================\n");
  printf(" Solder Check: GPIO Bridge & Short Detection\n");
  printf("====================================================\n");
  printf("Scanning /dev/gpio for testable pins...\n");

  DIR *dir = opendir("/dev/gpio");
  if (!dir) {
    printf("[FAIL] Failed to open /dev/gpio\n");
    return -1;
  }

  // Count available pin directory entries
  size_t count_pins = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    count_pins++;
  }

  if (count_pins == 0) {
    closedir(dir);
    printf("[!] No testable GPIO pins found in /dev/gpio\n");
    return 0;
  }

  pin_file_t *file_list = calloc(count_pins, sizeof(pin_file_t));
  if (!file_list) {
    closedir(dir);
    printf("[FAIL] Out of memory allocating pin list\n");
    return -1;
  }

  // Open candidate pins
  rewinddir(dir);
  size_t opened_count = 0;
  while ((ent = readdir(dir)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    if (opened_count >= count_pins)
      break;

    snprintf(file_list[opened_count].filename,
             sizeof(file_list[opened_count].filename), "/dev/gpio/%s",
             ent->d_name);
    file_list[opened_count].pin_num = atoi(ent->d_name);
    file_list[opened_count].fd = open(file_list[opened_count].filename, O_RDWR);
    if (file_list[opened_count].fd >= 0) {
      int fd = file_list[opened_count].fd;
      ioctl(fd, EOS_GPIO_IOCTL_SET_PULLUP);
      ioctl(fd, EOS_GPIO_IOCTL_SET_IOMODE_BIN);
    }
    opened_count++;
  }
  closedir(dir);

  // Compact into array of successfully opened pins
  pin_file_t *test_pins = calloc(opened_count, sizeof(pin_file_t));
  if (!test_pins) {
    for (size_t i = 0; i < opened_count; i++) {
      if (file_list[i].fd >= 0)
        close(file_list[i].fd);
    }
    free(file_list);
    printf("[FAIL] Out of memory\n");
    return -1;
  }

  size_t valid_count = 0;
  for (size_t i = 0; i < opened_count; i++) {
    if (file_list[i].fd >= 0) {
      test_pins[valid_count++] = file_list[i];
    }
  }

  // Sort pins by pin number for clean, deterministic output
  if (valid_count > 1) {
    qsort(test_pins, valid_count, sizeof(pin_file_t), compare_pins);
  }

  printf("Found %zu available user GPIO pin(s):\n  ", valid_count);
  for (size_t i = 0; i < valid_count; i++) {
    printf("GPIO %d%s", test_pins[i].pin_num,
           (i + 1 < valid_count) ? ", " : "\n");
  }

  if (valid_count < 2) {
    printf("[!] Need at least 2 open GPIO pins to test for bridges.\n");
    for (size_t i = 0; i < valid_count; i++) {
      close(test_pins[i].fd);
    }
    free(test_pins);
    free(file_list);
    return 0;
  }

  // ── Phase 1: Check for short to GND or + (VCC) ──────────────────
  printf("\n[Phase 1/2] Checking pin health (short to GND / +)...\n");
  size_t gnd_shorts = 0;
  size_t vcc_shorts = 0;

  for (size_t i = 0; i < valid_count; i++) {
    int fd = test_pins[i].fd;
    bool is_short_gnd = false;
    bool is_short_vcc = false;

    // Active write-and-readback test:
    // Drive alternating values (0, 1, 0, 1, 1, 0, 1, 0) and verify if the pin changes level.
    // If a pin is shorted to ground, it will NEVER read 1 (remains 0 despite writes to 1).
    // If a pin is shorted to +, it will NEVER read 0 (remains 1 despite writes to 0).
    static const uint8_t write_pattern[] = {0, 1, 0, 1, 1, 0, 1, 0};
    size_t zero_reads = 0;
    size_t one_reads = 0;
    size_t successful_writes = 0;

    for (size_t w = 0; w < sizeof(write_pattern); w++) {
      uint8_t out_val = write_pattern[w];
      if (write(fd, &out_val, 1) < 0) {
        break;
      }
      usleep(30);
      uint8_t in_val = 0;
      if (read(fd, &in_val, 1) > 0) {
        if (in_val == 0)
          zero_reads++;
        else if (in_val == 1)
          one_reads++;
        successful_writes++;
      }
    }

    if (successful_writes >= 4) {
      if (zero_reads == successful_writes && one_reads == 0) {
        is_short_gnd = true;
      } else if (one_reads == successful_writes && zero_reads == 0) {
        is_short_vcc = true;
      }
    }

    pin_reset_to_input_pullup(fd);

    if (is_short_gnd) {
      printf("  [!] GPIO %d: Does not change value after writes (stuck at 0) -> SHORTED TO GROUND (GND)\n",
             test_pins[i].pin_num);
      gnd_shorts++;
      test_pins[i].faulty = true;
    } else if (is_short_vcc) {
      printf("  [!] GPIO %d: Does not change value after writes (stuck at 1) -> SHORTED TO + (VCC)\n",
             test_pins[i].pin_num);
      vcc_shorts++;
      test_pins[i].faulty = true;
    }
  }

  if (gnd_shorts == 0 && vcc_shorts == 0) {
    printf("  [OK] All %zu pins toggled successfully and passed GND / + isolation check.\n",
           valid_count);
  }

  // ── Phase 2: Check for solder bridges between pin pairs ──────────
  printf("\n[Phase 2/2] Checking for solder bridges between pin pairs...\n");
  size_t bridges_found = 0;
  size_t pairs_tested = 0;

  for (size_t i = 0; i < valid_count; i++) {
    if (test_pins[i].faulty)
      continue;

    for (size_t j = i + 1; j < valid_count; j++) {
      if (test_pins[j].faulty)
        continue;

      pairs_tested++;

      // Test Direction 1: Drive pin i, read pin j
      bool match_ij = test_pin_bridge(test_pins[i].fd, test_pins[j].fd);
      pin_reset_to_input_pullup(test_pins[i].fd);
      pin_reset_to_input_pullup(test_pins[j].fd);

      // If Direction 1 matched, test reverse direction to confirm bridge
      bool match_ji = false;
      if (match_ij) {
        match_ji = test_pin_bridge(test_pins[j].fd, test_pins[i].fd);
        pin_reset_to_input_pullup(test_pins[j].fd);
        pin_reset_to_input_pullup(test_pins[i].fd);
      }

      if (match_ij && match_ji) {
        printf("  [!] SOLDER BRIDGE: GPIO %d <===> GPIO %d (bidirectional short)\n",
               test_pins[i].pin_num, test_pins[j].pin_num);
        bridges_found++;
      } else if (match_ij) {
        printf("  [!] LEAKAGE: GPIO %d ---> GPIO %d (unidirectional)\n",
               test_pins[i].pin_num, test_pins[j].pin_num);
        bridges_found++;
      }

      usleep(200); // Feed watchdog / yield between iterations
    }
  }

  // ── Results Summary ──────────────────────────────────────────────
  printf("----------------------------------------------------\n");
  printf("Summary: %zu pin(s), %zu pair(s) tested.\n", valid_count, pairs_tested);
  printf("  - Solder bridges: %zu\n", bridges_found);
  printf("  - GND shorts:     %zu\n", gnd_shorts);
  printf("  - VCC shorts:     %zu\n", vcc_shorts);
  printf("----------------------------------------------------\n");

  if (bridges_found == 0 && gnd_shorts == 0 && vcc_shorts == 0) {
    printf("[OK] SUCCESS: All pins isolated! No solder bridges detected.\n");
  } else {
    printf("[FAIL] Solder issues detected! Inspect connections on reported GPIOs.\n");
  }

  // Cleanup
  for (size_t i = 0; i < valid_count; i++) {
    close(test_pins[i].fd);
  }
  free(test_pins);
  free(file_list);

  return (bridges_found == 0 && gnd_shorts == 0 && vcc_shorts == 0) ? 0 : 1;
}

///////////////////////////////////////////////////////////////////////
// BIN MANIFEST:
///////////////////////////////////////////////////////////////////////
EOS_BIN_ATTR eos_bin_t soldercheck_app = {
  EOS_BIN_INITIALIZER,
  .flags       = 0,
  .filename    = "soldercheck",
  .name        = "Solder Check",
  .group       = "Diag",
  .description = "Probe GPIO pins with test patterns to detect solder bridges and shorts",
  .entry_point = soldercheck_main
};
///////////////////////////////////////////////////////////////////////
