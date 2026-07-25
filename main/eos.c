#include "app/app.h"
#include "board/board.h"
#include "sys/binfs.h"
#include "sys/capsmgr.h"
#include "sys/devfs.h"
#include "sys/device.h"
#include <stdio.h>

void app_main(void) {
  eos_capsmgr_init();
  eos_devtree_init();
  eos_board_init();
  eos_devfs_init();
  eos_binfs_init();

  // Run shell
  eos_system("ush");
}
