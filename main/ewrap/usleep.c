#include <unistd.h>
#include <esp_task_wdt.h>

extern int __real_usleep(useconds_t usec);

int __wrap_usleep(useconds_t usec) {
  (void)esp_task_wdt_reset();
  return __real_usleep(usec);
}
