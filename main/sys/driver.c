#include "sys/driver.h"
#include "misc/fancymacro.h"

uint32_t eos_drivers_count = 0;
eos_driver_t eos_drivers[EOS_MAX_DRIVERS];

eos_driver_t *eos_driver_find(char *scope, char *name) {
  for (int i = 0; i < eos_drivers_count; i++)
    if ((strcmp(scope, eos_drivers[i].scope) == 0) &&
        (strcmp(name, eos_drivers[i].name) == 0))
      return &eos_drivers[i];

  EOS_LOGE("Not found driver for %s/%s\n", scope, name);
  assert(0);
  return NULL;
}