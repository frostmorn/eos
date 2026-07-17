#include "driver/driver.h"
#include "misc/fancymacro.h"
#include "sys/device.h"

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

off_t eos_driver_lseek_empty(eos_dev_t *dev, off_t offset, int whence) {
  EOS_LOGW("Call lseek() not implemented for driver %s/%s", dev->driver->scope,
           dev->driver->name);
  return 0;
}

void eos_driver_shutdown_empty(eos_dev_t *dev) {
  EOS_LOGW("Call shutdown() not implemented for driver %s/%s",
           dev->driver->scope, dev->driver->name);
}

eos_driver_t *eos_driver_find(const char *scope, const char *name) {
  for (const eos_driver_t *driver = _eos_drivers_start;
       driver < _eos_drivers_end; ++driver) {

    if (strcmp(scope, driver->scope) == 0 && strcmp(name, driver->name) == 0) {
      EOS_LOGI("Found driver %s/%s", scope, name);
      return (eos_driver_t *)driver;
    }
  }

  EOS_LOGE("Not found driver %s/%s", scope, name);
  assert(0);
  return NULL;
}