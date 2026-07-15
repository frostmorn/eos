#include "board/board.h"
#include "sys/capsmgr.h"
#include "sys/devfs.h"
#include "sys/device.h"
#include <stdio.h>

void app_main(void) {
  eos_capsmgr_init();
  eos_devtree_init();
  eos_board_init();
  eos_devfs_init();
}
