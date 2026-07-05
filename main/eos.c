#include "board/board.h"
#include "sys/devtree.h"
#include <stdio.h>

void app_main(void) {
  eos_devtree_init();
  eos_board_init();
}
