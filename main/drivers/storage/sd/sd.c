#include "includes.h"
#ifdef DRIVER_STORAGE_SD_ENABLED

#include "driver.h"
int driver_storage_sd_init() { return 0; }

int driver_storage_sd_read(eos_dev_t *dev, void *buf, size_t len) { return 0;}

int driver_storage_sd_write(eos_dev_t *dev, void *buf, size_t len) {return 0;}

int driver_storage_sd_ioctl(eos_dev_t *dev, int cmd, va_list args) {return 0;}

void driver_storage_sd_shutdown(eos_dev_t *dev) {}

EOS_DRIVER_REG(storage, sd, EOS_INIT_DRIVERS_PRIO);

#endif