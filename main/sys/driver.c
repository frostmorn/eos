#include "sys/driver.h"
#include "misc/fancymacro.h"

uint32_t eos_driver_slot_count = 0;
EXT_RAM_BSS_ATTR eos_driver_t eos_driver_slots[EOS_MAX_DRIVERS];

eos_driver_t *eos_driver_find(char *scope, char *name) {

  for (int i = 0; i < eos_driver_slot_count; i++)
    if ((strcmp(scope, eos_driver_slots[i].scope) == 0) &&
        (strcmp(name, eos_driver_slots[i].name) == 0)) {
      EOS_LOGI("Found driver %s/%s", scope, name);
      return &eos_driver_slots[i];
    }

  EOS_LOGE("Not found driver for %s/%s", scope, name);
  assert(0);
  return NULL;
}