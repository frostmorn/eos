#include "pregen.h"
#ifdef EOS_TARGET_JC4880P443C_I_W_Y
#include "board/board.h"

void eos_board_init() {

  // ===================== I2C bus =====================
  // Shared between touch controller and ES8311 codec
  eos_dev_t *i2c = eos_dev_alloc();
  i2c->driver = eos_driver_find("bus", "i2c");
  i2c->pins = (eos_pin_t[]){{"sda_io_num", 7}, {"scl_io_num", 8}, {NULL, 0}};
  i2c->cfg = (eos_cfg_t[]){{"port", EOS_CFG_INT, .val.i = I2C_NUM_0},
                           {"clk_speed", EOS_CFG_INT, .val.i = 400000},
                           {NULL}};
  eos_dev_attach(i2c, eos_devtree_root());

  // ===================== DSI display =====================
  // ST7701S via MIPI DSI — no GPIO pin assignments needed,
  // DSI lanes are hardwired on ESP32-P4
  eos_dev_t *display = eos_dev_alloc();
  display->driver = eos_driver_find("display", "st7701s");
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
  touch->driver = eos_driver_find("input", "touch");
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
  codec->driver = eos_driver_find("sound", "es8311");
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
  speaker->driver = eos_driver_find("sound", "ns4150");
  speaker->pins = (eos_pin_t[]){{"ctrl_io_num", 20}, // PA_CTRL
                                {NULL, 0}};
  eos_dev_attach(speaker, eos_devtree_root());

  // ===================== Microphone =====================
  // MSM381A3729H9CP via ES8311 ADC
  eos_dev_t *mic = eos_dev_alloc();
  mic->driver = eos_driver_find("sensor", "microphone");
  eos_dev_attach(mic, codec);

  // ===================== SD card (SDMMC) =====================
  eos_dev_t *sd = eos_dev_alloc();
  sd->driver = eos_driver_find("storage", "sdmmc");
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
  battery->driver = eos_driver_find("sensor", "adc");
  battery->pins = (eos_pin_t[]){{"io_num", 53}, {NULL, 0}};
  battery->cfg = (eos_cfg_t[]){{"unit", EOS_CFG_INT, .val.i = ADC_UNIT_1},
                               {"channel", EOS_CFG_INT, .val.i = ADC_CHANNEL_0},
                               {"atten", EOS_CFG_INT, .val.i = ADC_ATTEN_DB_12},
                               {NULL}};
  eos_dev_attach(battery, eos_devtree_root());

  // ===================== RS485 =====================
  eos_dev_t *rs485 = eos_dev_alloc();
  rs485->driver = eos_driver_find("bus", "rs485");
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
  c6->driver = eos_driver_find("wireless", "esp32c6");
  c6->pins = (eos_pin_t[]){
      {"tx_io_num", 37}, // C6_U0TXD — NOTE: verify, may conflict with SD
      {"rx_io_num", 38}, // C6_U0RXD — NOTE: verify, may conflict with SD
      {NULL, 0}};
  eos_dev_attach(c6, eos_devtree_root());

  // ===================== Camera (CSI) =====================
  // Optional — JC4880P443C_I_W_Y variant includes camera
  eos_dev_t *cam = eos_dev_alloc();
  cam->driver = eos_driver_find("sensor", "camera");
  cam->cfg = (eos_cfg_t[]){// CSI lanes hardwired on ESP32-P4
                           {"width", EOS_CFG_INT, .val.i = 1920},
                           {"height", EOS_CFG_INT, .val.i = 1080},
                           {NULL}};
  eos_dev_attach(cam, eos_devtree_root());
}
#endif