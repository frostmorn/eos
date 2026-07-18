#include "tests.h"
#include "cmdapps/disp_test_bars/disp_test_bars.h"
#include "cmdapps/disp_test_colors/disp_test_colors.h"
#include "cmdapps/tree/tree.h"
#include "sys/devfs.h"

void eos_tests_init() {
  // Devfs tree
  char *tree_args[] = {"tree", "/dev"};
  tree_main(2, tree_args);

  // Display test colors
  char *disp_test_colors_args[] = {"disp_test_colors", "/dev/display0"};
  disp_test_colors_main(2, disp_test_colors_args);

  // Display test bars
  char *disp_test_bars_args[] = {"disp_test_bars", "/dev/display0"};
  disp_test_bars_main(2, disp_test_bars_args);
}