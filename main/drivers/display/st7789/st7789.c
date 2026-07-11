#include "includes.h"
#ifdef EOS_DRIVER_DISPLAY_ST7789_ENABLED
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

///////////////////////////////////////////////////////
#include "sys/driver.h"

int driver_display_st7789_init(eos_dev_t *dev) { return 0; }

int driver_display_st7789_read(eos_dev_t *dev, void *buf, size_t len) {
  return 0;
}

int driver_display_st7789_write(eos_dev_t *dev, void *buf, size_t len) {
  return 0;
}

int driver_display_st7789_ioctl(eos_dev_t *dev, int cmd, ...) { return 0; }

void driver_display_st7789_shutdown(eos_dev_t *dev) {}

EOS_DRIVER_REG(display, st7789, EOS_INIT_DRIVERS_PRIO);

#endif