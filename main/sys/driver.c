#include "sys/driver.h"
#include "misc/fancymacro.h"
#include "sys/device.h"

EXT_RAM_BSS_ATTR eos_driver_t *eos_driver_slots[EOS_MAX_DRIVERS];



bool eos_driver_init_empty(eos_dev_t *dev) {
  EOS_LOGW("Call init() not implemented for driver %s/%s", dev->driver->scope,
           dev->driver->name);
  return true;
}

int eos_driver_read_empty(eos_dev_t *dev, void *buf, size_t len) {
  EOS_LOGW("Call read() not implemented for driver %s/%s", dev->driver->scope,
           dev->driver->name);
  return 0;
}

int eos_driver_write_empty(eos_dev_t *dev, void *buf, size_t len) {
  EOS_LOGW("Call write() not implemented for driver %s/%s", dev->driver->scope,
           dev->driver->name);
  return 0;
}

int eos_driver_ioctl_empty(eos_dev_t *dev, int cmd, ...) {
  EOS_LOGW("Call ioctl() not implemented for driver %s/%s", dev->driver->scope,
           dev->driver->name);
  return 0;
}

void eos_driver_shutdown_empty(eos_dev_t *dev) {
  EOS_LOGW("Call shutdown() not implemented for driver %s/%s",
           dev->driver->scope, dev->driver->name);
}

eos_driver_t *eos_driver_find(const char *scope, const char *name) {
  for (int32_t i = 0; i < EOS_MAX_DRIVERS; i++) {
    eos_driver_t *driver = eos_driver_slots[i];

    if (driver == NULL)
      continue;

    if (strcmp(scope, driver->scope) == 0 && strcmp(name, driver->name) == 0) {
      EOS_LOGI("Found driver %s/%s", scope, name);
      return driver;
    }
  }

  EOS_LOGE("Not found driver for %s/%s", scope, name);
  assert(0);
  return NULL;
}

eos_error_t eos_driver_reg(eos_driver_t *driver) {
  // Check driver validness
  if (!driver) {
    return EOS_ERR_DEVICE_DRIVER_INVALID;
  }

  EOS_LOGI("Registering driver %s/%s", driver->scope, driver->name);

  // Find first empty slot
  eos_driver_t **slot = NULL;
  for (int32_t i = 0; i < EOS_MAX_DRIVERS; i++) {
    if (eos_driver_slots[i] == driver) {
      return EOS_ERR_DRIVER_ALREADY_REGISTERED;
    }

    if (eos_driver_slots[i] == NULL && slot == NULL) {
      slot = &eos_driver_slots[i];
    }
  }

  // Slot not found, time to fail
  if (!slot) {
    EOS_LOGE("Not enough slots to init driver for %s/%s\n", driver->scope,
             driver->name);
    abort();
  }

  // Replace empty driver calls with dummies
  if (driver->init == NULL)
    driver->init = eos_driver_init_empty;

  if (driver->read == NULL)
    driver->read = eos_driver_read_empty;

  if (driver->write == NULL)
    driver->write = eos_driver_write_empty;

  if (driver->ioctl == NULL)
    driver->ioctl = eos_driver_ioctl_empty;

  if (driver->shutdown == NULL)
    driver->shutdown = eos_driver_shutdown_empty;

  // Registering driver
  *slot = driver;

  return EOS_ERR_NO_ERROR;
}