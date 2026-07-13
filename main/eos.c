#include "board/board.h"
#include "sys/capsmgr.h"
#include "sys/devtree.h"
#include <stdio.h>

void app_main(void) {
  eos_capsmgr_init();
  eos_devtree_init();
  eos_board_init();
}
