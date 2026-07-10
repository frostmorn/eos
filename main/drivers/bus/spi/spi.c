#include "includes.h"
#ifdef EOS_DRIVER_BUS_SPI_ENABLED

#include "drivers/bus/bus.h"
#include "sys/driver.h"

int driver_bus_spi_init(eos_dev_t *dev_bus) { return 0; }

int driver_bus_spi_read(eos_dev_t *dev_bus, void *buf, size_t len) { return 0; }

int driver_bus_spi_write(eos_dev_t *dev_bus, void *buf, size_t len) {
  return 0;
}

int driver_bus_spi_ioctl(eos_dev_t *dev_bus, int cmd, ...) { return 0; }

void driver_bus_spi_shutdown(eos_dev_t *dev_bus) {}

EOS_DRIVER_REG(bus, spi, EOS_INIT_DRIVERS_BUS);

#endif