#include "ecore/binfs.h"
#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/devfs.h"
#include "ecore/elf.h"
#include "ecore/log.h"
#include "ecore/pipefs.h"
#include "ecore/proc.h"
#include "ecore/procfs.h"
#include "ecore/tmpfs.h"
#include "ecore/vfs.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp_private/rtc_clk.h"
#endif

#define EOS_MAIN_TASK_STACK_SIZE 32768

// To make Application context actually work, we've to be inside pthread
void *eos_main(void *data) {
  (void)data;
  eos_proc_init();
  eos_vfs_init();
  eos_tmpfs_init();
  eos_log_init();
  eos_procfs_init();
  eos_pipefs_init();
  eos_capsmgr_init();
  eos_devtree_init();
  eos_devfs_init();
  eos_binfs_init();
  eos_elf_init_symbols();

  while (1) {
    // Run init script (falls back to ush if init fails)
    if (system("init.sh") < 0) {
      system("ush");
    }
    usleep(1000000);
  }
}

void app_main(void) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // Keep BBPLL clock alive so USB Serial/JTAG PHY clock is never removed by sleep hooks
  rtc_clk_bbpll_add_consumer();
#endif
  // Time to launch main thread

  pthread_t eos_main_thread;
  pthread_attr_t attr;

  pthread_attr_init(&attr);

  pthread_attr_setstacksize(&attr, EOS_MAIN_TASK_STACK_SIZE);

  pthread_create(&eos_main_thread, &attr, eos_main, NULL);
}
