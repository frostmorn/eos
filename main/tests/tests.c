#include "tests.h"
#include "cmdapps/tree/tree.h"
#include "sys/devfs.h"

void eos_tests_init() {
  char *args[] = {"tree", "/dev"};
  tree_main(2, args);
}