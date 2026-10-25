#include <unistd.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ecore/threadctx.h"

extern int __real_usleep(useconds_t usec);

int __wrap_usleep(useconds_t usec) {
  if (xTaskGetCurrentTaskHandle() != NULL) {
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      (void)esp_task_wdt_reset();
    }
    (void)eos_tctx_get();
  }
  return __real_usleep(usec);
}
