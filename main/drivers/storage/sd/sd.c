#include "device.h"
#include "includes.h"

int driver_storage_sd_init() {}

int driver_storage_sd_read(eos_dev_t *dev, void *buf, size_t len) {}

int driver_storage_sd_write(eos_dev_t *dev, void *buf, size_t len) {}

int driver_storage_sd_ioctl(eos_dev_t *dev, int cmd, va_list args) {}

void driver_storage_sd_ioctl_shutdown(eos_dev_t *dev) {}

eos_driver_t driver_storage_sd = {.init = driver_storage_sd_init,
                                  .read = driver_storage_sd_read,
                                  .write = driver_storage_sd_write,
                                  .shutdown = driver_storage_sd_ioctl_shutdown};

EOS_DRIVER_REG(storage, sd);
