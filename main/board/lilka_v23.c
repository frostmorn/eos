#include "pregen.h"
#ifdef EOS_TARGET_LILKAV2
#include "board.h"

void eos_board_init() {
  // ===================== SPI bus =====================
  eos_dev_t *spi = eos_dev_alloc();
  spi->driver = eos_driver_find("bus", "spi");
  spi->pins = (eos_pin_t[]){{"sck", 18}, {"mosi", 17}, {"miso", 8}, {NULL, 0}};
  eos_dev_attach(spi, eos_devtree_root());

  // ================= Display ST7789 ==================
  eos_dev_t *display = eos_dev_alloc();
  display->driver = eos_driver_find("display", "st7789");
  display->pins = (eos_pin_t[]){{"cs", 7}, {"dc", 15}, {NULL, 0}};
  display->cfg = (eos_cfg_t[]){
      {"rotation", "3"}, {"width", "240"}, {"height", "280"}, {NULL, NULL}};
  eos_dev_attach(display, spi);

  // ==================== SD card ======================
  eos_dev_t *sd = eos_dev_alloc();
  sd->driver = eos_driver_find("storage", "sd");
  sd->pins = (eos_pin_t[]){{"cs", 16}, {NULL, 0}};
  eos_dev_attach(sd, spi);

  // ================== GPIO buttons ===================
  eos_dev_t *gpio = eos_dev_alloc();
  gpio->driver = eos_driver_find("bus", "gpio");

  // TODO: This should be populated to different device, gpio_keyboard?
  // gpio->pins =
  //     (eos_pin_t[]){{"up", 38},    {"down", 41}, {"left", 39},  {"right",
  //     40},
  //                   {"select", 0}, {"start", 4}, {"a", 5},      {"b", 6},
  //                   {"c", 10},     {"d", 9},     {"sleep", 46}, {NULL, 0}};

  eos_dev_attach(gpio, eos_devtree_root());

  // ==================== Buzzer =======================
  eos_dev_t *buzzer = eos_dev_alloc();
  buzzer->driver = eos_driver_find("sound", "buzzer");
  buzzer->pins = (eos_pin_t[]){{"out", 11}, {NULL, 0}};
  eos_dev_attach(buzzer, eos_devtree_root());

  // ==================== I2S audio ====================
  eos_dev_t *i2s = eos_dev_alloc();
  i2s->driver = eos_driver_find("sound", "max98357");
  i2s->pins = (eos_pin_t[]){{"bclk", 42}, {"dout", 2}, {"lrck", 1}, {NULL, 0}};
  eos_dev_attach(i2s, eos_devtree_root());

  // ================== Battery ADC ====================
  eos_dev_t *battery = eos_dev_alloc();
  battery->driver = eos_driver_find("sensor", "adc");
  battery->pins = (eos_pin_t[]){{"adc", 3}, {NULL, 0}};
  battery->cfg = (eos_cfg_t[]){{"channel", "ADC1_CHANNEL_2"}, {NULL, NULL}};
  eos_dev_attach(battery, eos_devtree_root());
}
#endif