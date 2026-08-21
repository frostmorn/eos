#include "eboard/board.h"
#include "ecore/binfs.h"
#include "ecore/capsmgr.h"
#include "ecore/devfs.h"
#include "ecore/device.h"
#include "ecore/rootfs.h"
#include "ecore/threadctx.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define EOS_MAIN_TASK_STACK_SIZE 8192

// To make Application context actually work, we've to be inside pthread
void *eos_main(void *data) {

  while (1) {
    // Run shell
    system("ush");
  }
}

void app_main(void) {
  eos_rootfs_init();
  eos_capsmgr_init();
  eos_devtree_init();
  eos_board_init();
  eos_devfs_init();
  eos_binfs_init();
  eos_tctx_init();

  // Time to launch main thread

  pthread_t eos_main_thread;
  pthread_attr_t attr;

  pthread_attr_init(&attr);

  pthread_attr_setstacksize(&attr, EOS_MAIN_TASK_STACK_SIZE);

  pthread_create(&eos_main_thread, &attr, eos_main, NULL);
  while (1) {
    // never exit temporary till not fix strange non freeable memory allocated with malloc
    vTaskDelay(portMAX_DELAY);
  }
}
