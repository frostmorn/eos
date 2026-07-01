#include "devtree.h"
#include "driver.h"

eos_dev_t eos_root_dev;

void eos_devtree_init() {
  eos_root_dev = (eos_dev_t){.driver = &(eos_drivers[0]),
                             .config = NULL,
                             .state = NULL,
                             .parent = NULL,
                             .child = NULL};
}

eos_dev_t *eos_devtree_root() { return &eos_root_dev; }