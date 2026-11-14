#include <unistd.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ecore/proc.h"

extern int __real_usleep(useconds_t usec);

int __wrap_usleep(useconds_t usec) {
  if (xTaskGetCurrentTaskHandle() != NULL) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      (void)esp_task_wdt_reset();
    }
    (void)eos_proc_current();
  }
  bool prev_k = eos_proc_enter_kernel();
  bool prev_w = eos_proc_enter_wait();
  int ret = __real_usleep(usec);
  eos_proc_leave_wait(prev_w);
  eos_proc_leave_kernel(prev_k);
  return ret;
}
