#include "includes.h"
#ifdef DRIVER_BUS_SPI_ENABLED

#include "driver.h"

int driver_bus_spi_init() { return 0;}

int driver_bus_spi_read(eos_dev_t *dev, void *buf, size_t len) { return 0;}

int driver_bus_spi_write(eos_dev_t *dev, void *buf, size_t len) { return 0;}

int driver_bus_spi_ioctl(eos_dev_t *dev, int cmd, va_list args) { return 0;}

void driver_bus_spi_shutdown(eos_dev_t *dev) {}

EOS_DRIVER_REG(bus, spi, EOS_INIT_DRIVERS_BUS);

#endif