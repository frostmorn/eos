#include "pregen.h"
#ifdef EOS_TARGET_LILKAV2
#include "board/board.h"

void eos_board_init() {

  // ===================== SPI bus =====================
  eos_dev_t *spi = eos_dev_alloc();
  spi->driver = eos_driver_find("bus", "spi");
  spi->pins = (eos_pin_t[]){
      {"sclk", 18}, {"mosi", 17}, {"miso", 8}, {NULL, 0}};
  spi->cfg = (eos_cfg_t[]){{"host", EOS_CFG_INT, .val.i = SPI2_HOST},
                           {"dma", EOS_CFG_INT, .val.i = SPI_DMA_CH_AUTO},
                           {NULL}};
  eos_dev_attach(spi, eos_devtree_root());

  // ================= Display ST7789 ==================
  eos_dev_t *display = eos_dev_alloc();
  display->driver = eos_driver_find("display", "st7789");
  display->pins = (eos_pin_t[]){{"cs", 7}, {"dc", 15}, {"en", 46}, {NULL, 0}};
  display->cfg = (eos_cfg_t[]){
      {"clock_speed_hz", EOS_CFG_INT, .val.i = SPI_MASTER_FREQ_80M},
      {"rotation", EOS_CFG_INT, .val.i = 3},
      {"width", EOS_CFG_INT, .val.i = 240},
      {"height", EOS_CFG_INT, .val.i = 280},
      {NULL}};
  eos_dev_attach(display, spi);

  // ==================== SD card ======================
  eos_dev_t *sd = eos_dev_alloc();
  sd->driver = eos_driver_find("storage", "sd");
  sd->pins = (eos_pin_t[]){{"cs", 16}, {NULL, 0}};
  sd->cfg = (eos_cfg_t[]){
      {"clock_speed_hz", EOS_CFG_INT, .val.i = SPI_MASTER_FREQ_20M}, {NULL}};
  eos_dev_attach(sd, spi);

  // ================== GPIO bus =======================
  eos_dev_t *gpio = eos_dev_alloc();
  gpio->driver = eos_driver_find("bus", "gpio");
  eos_dev_attach(gpio, eos_devtree_root());

  // ================== Input keyboard =================
  // eos_dev_t *kbd = eos_dev_alloc();
  // kbd->driver = eos_driver_find("input", "keyboard");
  // kbd->pins =
  //     (eos_pin_t[]){{"up", 38},    {"down", 41}, {"left", 39},  {"right",
  //     40},
  //                   {"select", 0}, {"start", 4}, {"a", 5},      {"b", 6},
  //                   {"c", 10},     {"d", 9},     {"sleep", 46}, {NULL, 0}};
  // kbd->cfg = (eos_cfg_t[]){{"mode", EOS_CFG_INT, .val.i = GPIO_MODE_INPUT},
  //                          {"pull", EOS_CFG_INT, .val.i = GPIO_PULLUP_ONLY},
  //                          {NULL}};
  // eos_dev_attach(kbd, gpio);

  // ==================== Buzzer =======================
  // eos_dev_t *buzzer = eos_dev_alloc();
  // buzzer->driver = eos_driver_find("sound", "buzzer");
  // buzzer->pins = (eos_pin_t[]){{"gpio_num", 11}, {NULL, 0}};
  // eos_dev_attach(buzzer, eos_devtree_root());

  // // ==================== I2S audio ====================
  // eos_dev_t *i2s = eos_dev_alloc();
  // i2s->driver = eos_driver_find("sound", "max98357");
  // i2s->pins = (eos_pin_t[]){
  //     {"bclk_io_num", 42}, {"dout_io_num", 2}, {"ws_io_num", 1}, {NULL, 0}};
  // i2s->cfg = (eos_cfg_t[]){
  //     {"role", EOS_CFG_INT, .val.i = I2S_ROLE_MASTER},
  //     {"data_bits", EOS_CFG_INT, .val.i = I2S_DATA_BIT_WIDTH_16BIT},
  //     {"sample_rate", EOS_CFG_INT, .val.i = 44100},
  //     {NULL}};
  // eos_dev_attach(i2s, eos_devtree_root());

  // // ================== Battery ADC ====================
  // eos_dev_t *battery = eos_dev_alloc();
  // battery->driver = eos_driver_find("sensor", "adc");
  // battery->pins = (eos_pin_t[]){{"io_num", 3}, {NULL, 0}};
  // battery->cfg = (eos_cfg_t[]){{"unit", EOS_CFG_INT, .val.i = ADC_UNIT_1},
  //                              {"channel", EOS_CFG_INT, .val.i =
  //                              ADC_CHANNEL_2},
  //                              {"atten", EOS_CFG_INT, .val.i =
  //                              ADC_ATTEN_DB_12}, {NULL}};
  // eos_dev_attach(battery, eos_devtree_root());
}
#endif