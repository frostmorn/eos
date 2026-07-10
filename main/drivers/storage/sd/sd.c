#include "includes.h"
#ifdef EOS_DRIVER_STORAGE_SD_ENABLED

#include "sys/driver.h"
int driver_storage_sd_init(eos_dev_t *dev) { return 0; }

int driver_storage_sd_read(eos_dev_t *dev, void *buf, size_t len) { return 0; }

int driver_storage_sd_write(eos_dev_t *dev, void *buf, size_t len) { return 0; }

int driver_storage_sd_ioctl(eos_dev_t *dev, int cmd, ...) { return 0; }

void driver_storage_sd_shutdown(eos_dev_t *dev) {}

EOS_DRIVER_REG(storage, sd, EOS_INIT_DRIVERS_PRIO);

#endif