#include "tests.h"
#include "app/app.h"
#include "sys/devfs.h"

void eos_tests_init() {
  // Devfs tree
  eos_system("tree /dev");

  // Display test colors
  eos_system("disp_test_colors /dev/display0");

  // Display test bars
  eos_system("disp_test_bars /dev/display0");

  // Run shell
  eos_system("ush");
}