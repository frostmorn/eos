#include "pregen.h"
#ifdef EOS_TARGET_JC4880P443C_I_W_Y
#include "eboard/board.h"
#include <driver/gpio.h>

void eos_board_init() {
  // ===================== DSI display =====================
  // ST7701S via MIPI DSI
  eos_dev_t *display = eos_dev_alloc();
  display->driver = eos_drv_find("display", "st7701s");
  display->pins = (eos_pin_t[]){{"rst", 5},
                                {"backlight", 23},
                                {NULL, 0}};
  display->cfg = (eos_cfg_t[]){{"width", EOS_CFG_INT, .val.i = 480},
                               {"height", EOS_CFG_INT, .val.i = 800},
                               {"rotation", EOS_CFG_INT, .val.i = 0},
                               {"lane_num", EOS_CFG_INT, .val.i = 2},
                               {"bitrate_mbps", EOS_CFG_INT, .val.i = 750},
                               {"dpi_clk_mhz", EOS_CFG_INT, .val.i = 28},
                               {"ldo_chan", EOS_CFG_INT, .val.i = 3},
                               {"ldo_voltage_mv", EOS_CFG_INT, .val.i = 2500},
                               {NULL}};
  eos_dev_attach(display, eos_devtree_root());

#ifdef EOS_DRV_BUS_I2C_ENABLED
  // ===================== I2C bus =====================
  // Shared between touch controller (GT911) and audio codec (ES8311)
  eos_dev_t *i2c = eos_dev_alloc();
  i2c->driver = eos_drv_find("bus", "i2c");
  i2c->pins = (eos_pin_t[]){{"sda", 7}, {"scl", 8}, {NULL, 0}};
  i2c->cfg = (eos_cfg_t[]){{"port", EOS_CFG_INT, .val.i = 0},
                           {"clk_speed", EOS_CFG_INT, .val.i = 400000},
                           {NULL}};
  eos_dev_attach(i2c, eos_devtree_root());

#ifdef EOS_DRV_INPUT_GT911_ENABLED
  // ===================== Touch controller =====================
  // GT911 capacitive touch screen attached to generic I2C bus
  eos_dev_t *touch = eos_dev_alloc();
  touch->driver = eos_drv_find("input", "gt911");
  touch->pins = (eos_pin_t[]){{"rst", -1}, {"int", -1}, {NULL, 0}};
  touch->cfg = (eos_cfg_t[]){{"clk_speed", EOS_CFG_INT, .val.i = 400000},
                             {"width", EOS_CFG_INT, .val.i = 480},
                             {"height", EOS_CFG_INT, .val.i = 800},
                             {"addr", EOS_CFG_INT, .val.i = 0x5D},
                             {NULL}};
  eos_dev_attach(touch, i2c);
#endif
#endif

#ifdef EOS_DRV_BUS_GPIO_ENABLED
  // ===================== GPIO bus =====================
  eos_dev_t *gpio = eos_dev_alloc();
  gpio->driver = eos_drv_find("bus", "gpio");
  eos_dev_attach(gpio, eos_devtree_root());

#ifdef EOS_DRV_INPUT_KEYBOARD_ENABLED
  // ===================== GPIO keyboard =====================
  // JC4880 board has a BOOT button on GPIO 35 (active-low with pullup).
  // Map it to button B (standard back/cancel/exit button in EOS).
  eos_dev_t *kbd = eos_dev_alloc();
  kbd->driver = eos_drv_find("input", "keyboard");
  kbd->pins = (eos_pin_t[]){{"b", 35}, {NULL, 0}};
  kbd->cfg = (eos_cfg_t[]){{"mode", EOS_CFG_INT, .val.i = GPIO_MODE_INPUT},
                           {"pull", EOS_CFG_INT, .val.i = GPIO_PULLUP_ONLY},
                           {NULL}};
  eos_dev_attach(kbd, gpio);
#endif
#endif

#ifdef EOS_DRV_NET_WIFI_ENABLED
  // ===================== WiFi Radio =====================
  // Guition JC4880P443C uses an external ESP32-C6 coprocessor connected over
  // SDIO (GPIO 14-19) with C6_CHIP_PU hardware reset control on GPIO 54.
  eos_dev_t *wifi = eos_dev_alloc();
  wifi->driver = eos_drv_find("net", "wifi");
  wifi->pins = (eos_pin_t[]){{"chip_pu", 54}, {NULL, 0}};
  eos_dev_attach(wifi, eos_devtree_root());

#ifdef EOS_DRV_NET_STA_ENABLED
  // ===================== WiFi Station ===================
  eos_dev_t *sta = eos_dev_alloc();
  sta->driver = eos_drv_find("net", "sta");
  eos_dev_attach(sta, wifi);
#endif

#ifdef EOS_DRV_NET_AP_ENABLED
  // ===================== WiFi Access Point ==============
  eos_dev_t *ap = eos_dev_alloc();
  ap->driver = eos_drv_find("net", "ap");
  eos_dev_attach(ap, wifi);
#endif
#endif

  /*
  // ===================== I2C bus =====================
  // Shared between touch controller and ES8311 codec
  eos_dev_t *i2c = eos_dev_alloc();
  i2c->driver = eos_drv_find("bus", "i2c");
  i2c->pins = (eos_pin_t[]){{"sda_io_num", 7}, {"scl_io_num", 8}, {NULL, 0}};
  i2c->cfg = (eos_cfg_t[]){{"port", EOS_CFG_INT, .val.i = I2C_NUM_0},
                           {"clk_speed", EOS_CFG_INT, .val.i = 400000},
                           {NULL}};
  eos_dev_attach(i2c, eos_devtree_root());

  // ===================== DSI display =====================
  // ST7701S via MIPI DSI — no GPIO pin assignments needed,
  // DSI lanes are hardwired on ESP32-P4
  eos_dev_t *display = eos_dev_alloc();
  display->driver = eos_drv_find("display", "st7701s");
  display->pins = (eos_pin_t[]){{"backlight_pwm", 45}, // LCD_PWM
                                {NULL, 0}};
  display->cfg = (eos_cfg_t[]){{"width", EOS_CFG_INT, .val.i = 480},
                               {"height", EOS_CFG_INT, .val.i = 800},
                               {"rotation", EOS_CFG_INT, .val.i = 0},
                               {NULL}};
  eos_dev_attach(display, eos_devtree_root());

  // ===================== Touch controller =====================
  // GT911 or similar CTP via I2C
  eos_dev_t *touch = eos_dev_alloc();
  touch->driver = eos_drv_find("input", "touch");
  touch->pins = (eos_pin_t[]){{"int_io_num", 21}, // TOUCH_INT
                              {"rst_io_num", 22}, // TOUCH_RST
                              {NULL, 0}};
  touch->cfg = (eos_cfg_t[]){{"addr", EOS_CFG_INT, .val.i = 0x5D},
                             {"width", EOS_CFG_INT, .val.i = 480},
                             {"height", EOS_CFG_INT, .val.i = 800},
                             {NULL}};
  eos_dev_attach(touch, i2c);

  // ===================== ES8311 audio codec =====================
  eos_dev_t *codec = eos_dev_alloc();
  codec->driver = eos_drv_find("sound", "es8311");
  codec->pins = (eos_pin_t[]){{"mclk_io_num", 13}, // CODEC_I2S0_MCLK
                              {"bclk_io_num", 12}, // CODEC_I2S0_SCLK
                              {"lrck_io_num", 10}, // CODEC_I2S0_LRCK
                              {"data_io_num", 9},  // CODEC_I2S0_DSDIN
                              {NULL, 0}};
  codec->cfg = (eos_cfg_t[]){{"i2c_addr", EOS_CFG_INT, .val.i = 0x18},
                             {"sample_rate", EOS_CFG_INT, .val.i = 44100},
                             {"bits", EOS_CFG_INT, .val.i = 16},
                             {NULL}};
  eos_dev_attach(codec, i2c);

  // ===================== Speaker amplifier =====================
  // NS4150 controlled via PA_CTRL
  eos_dev_t *speaker = eos_dev_alloc();
  speaker->driver = eos_drv_find("sound", "ns4150");
  speaker->pins = (eos_pin_t[]){{"ctrl_io_num", 20}, // PA_CTRL
                                {NULL, 0}};
  eos_dev_attach(speaker, eos_devtree_root());

  // ===================== Microphone =====================
  // MSM381A3729H9CP via ES8311 ADC
  eos_dev_t *mic = eos_dev_alloc();
  mic->driver = eos_drv_find("sensor", "microphone");
  eos_dev_attach(mic, codec);

  // ===================== SD card (SDMMC) =====================
  eos_dev_t *sd = eos_dev_alloc();
  sd->driver = eos_drv_find("storage", "sdmmc");
  sd->pins = (eos_pin_t[]){{"clk_io_num", 36}, // SD_CLK
                           {"cmd_io_num", 37}, // SD_CMD
                           {"d0_io_num", 38},  // SD_DATA0
                           {"d1_io_num", 39},  // SD_DATA1
                           {"d2_io_num", 40},  // SD_DATA2
                           {"d3_io_num", 41},  // SD_DATA3
                           {NULL, 0}};
  sd->cfg = (eos_cfg_t[]){{"bus_width", EOS_CFG_INT, .val.i = 4},
                          {"max_freq_khz", EOS_CFG_INT, .val.i = 40000},
                          {NULL}};
  eos_dev_attach(sd, eos_devtree_root());

  // ===================== Battery ADC =====================
  eos_dev_t *battery = eos_dev_alloc();
  battery->driver = eos_drv_find("sensor", "adc");
  battery->pins = (eos_pin_t[]){{"io_num", 53}, {NULL, 0}};
  battery->cfg = (eos_cfg_t[]){{"unit", EOS_CFG_INT, .val.i = ADC_UNIT_1},
                               {"channel", EOS_CFG_INT, .val.i = ADC_CHANNEL_0},
                               {"atten", EOS_CFG_INT, .val.i = ADC_ATTEN_DB_12},
                               {NULL}};
  eos_dev_attach(battery, eos_devtree_root());

  // ===================== RS485 =====================
  eos_dev_t *rs485 = eos_dev_alloc();
  rs485->driver = eos_drv_find("bus", "rs485");
  rs485->pins = (eos_pin_t[]){{"tx_io_num", 26}, // TX1
                              {"rx_io_num", 27}, // RX1
                              {NULL, 0}};
  rs485->cfg = (eos_cfg_t[]){{"baud_rate", EOS_CFG_INT, .val.i = 115200},
                             {"port", EOS_CFG_INT, .val.i = UART_NUM_1},
                             {NULL}};
  eos_dev_attach(rs485, eos_devtree_root());

  // ===================== ESP32-C6 (WiFi/BT) =====================
  // Secondary controller via UART
  eos_dev_t *c6 = eos_dev_alloc();
  c6->driver = eos_drv_find("wireless", "esp32c6");
  c6->pins = (eos_pin_t[]){
      {"tx_io_num", 37}, // C6_U0TXD — NOTE: verify, may conflict with SD
      {"rx_io_num", 38}, // C6_U0RXD — NOTE: verify, may conflict with SD
      {NULL, 0}};
  eos_dev_attach(c6, eos_devtree_root());

  // ===================== Camera (CSI) =====================
  // Optional — JC4880P443C_I_W_Y variant includes camera
  eos_dev_t *cam = eos_dev_alloc();
  cam->driver = eos_drv_find("sensor", "camera");
  cam->cfg = (eos_cfg_t[]){// CSI lanes hardwired on ESP32-P4
                           {"width", EOS_CFG_INT, .val.i = 1920},
                           {"height", EOS_CFG_INT, .val.i = 1080},
                           {NULL}};
  eos_dev_attach(cam, eos_devtree_root());
  */
}
#endif
