#include "includes.h"
#ifdef DRIVER_STORAGE_SD_ENABLED

#include "device.h"
int driver_storage_sd_init() {}

int driver_storage_sd_read(eos_dev_t *dev, void *buf, size_t len) {}

int driver_storage_sd_write(eos_dev_t *dev, void *buf, size_t len) {}

int driver_storage_sd_ioctl(eos_dev_t *dev, int cmd, va_list args) {}

void driver_storage_sd_shutdown(eos_dev_t *dev) {}

EOS_DRIVER_REG(storage, sd);

#endif