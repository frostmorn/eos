#pragma once

#include <soc/soc_caps.h>

#define EOS_TARGET_ESP32S3_QEMU

#define EOS_MAX_DEVICES 32

// Enabled drivers:
#define EOS_DRV_STORAGE_PARTITION_ENABLED
#define EOS_DRV_STORAGE_FLASH_ENABLED
#define EOS_DRV_BUS_GPIO_ENABLED
#define EOS_DRV_BUS_SPI_ENABLED

// CPU Core count
#define EOS_CPU_COUNT SOC_CPU_CORES_NUM

// Pin configuration:
#define EOS_GPIO_COUNT SOC_GPIO_PIN_COUNT

#define EOS_GPIO_RESERVED                                                      \
  {19, 20, 22, 23, 24, 25, 26, 27, 28, 29,                                     \
   30, 31, 32, 33, 34, 35, 36, 37, 43, 44}

// I2C
#define EOS_I2C_COUNT SOC_I2C_NUM

// SPI:
#define EOS_SPI_COUNT SOC_SPI_PERIPH_NUM
#define EOS_SPI_RESERVED {0}

// UART:
#define EOS_UART_COUNT SOC_UART_NUM
#define EOS_UART_RESERVED {0}
