#include "eboard/board.h"
#include "ecore/app.h"
#include "ecore/binfs.h"
#include "ecore/capsmgr.h"
#include "ecore/devfs.h"
#include "ecore/device.h"
#include "ecore/rootfs.h"
#include <stdio.h>

void app_main(void) {
  eos_capsmgr_init();
  eos_devtree_init();
  eos_board_init();
  eos_devfs_init();
  eos_binfs_init();
  eos_rootfs_init();

  // Run shell
  eos_system("ush");
}
