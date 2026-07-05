#include "pregen.h"
#ifdef EOS_TARGET_LILKAV2
#include "board.h"
void eos_board_init() {
  eos_dev_t *spi = eos_dev_alloc();
  //   spi->driver = EOS_DRIVER(bus, spi);
  //   spi->config = EOS_DRIVER_CONFIG(bus, spi, .mosi = 11, .miso = 13, .sck =
  //   12);
  eos_dev_attach(spi, eos_devtree_root());
}
#endif