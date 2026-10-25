#pragma once

#include <soc/soc_caps.h>

#define EOS_TARGET_JC4880P443C_I_W_Y

#define EOS_MAX_DEVICES 32

// Enabled drivers:
#define EOS_DRV_STORAGE_PARTITION_ENABLED
#define EOS_DRV_STORAGE_FLASH_ENABLED
#define EOS_DRV_BUS_GPIO_ENABLED
#define EOS_DRV_BUS_I2C_ENABLED
#define EOS_DRV_BUS_SPI_ENABLED
#define EOS_DRV_DISPLAY_ST7701S_ENABLED
#define EOS_DRV_INPUT_GT911_ENABLED
#define EOS_DRV_INPUT_KEYBOARD_ENABLED

// CPU Core count
#define EOS_CPU_COUNT SOC_CPU_CORES_NUM

// Pin configuration:
#define EOS_GPIO_COUNT SOC_GPIO_PIN_COUNT

// Reserved pins:
// Flash, PSRAM, and system pins are claimed dynamically via esp_gpio_is_reserved().
#define EOS_GPIO_RESERVED {-1}

// I2C
#define EOS_I2C_COUNT SOC_I2C_NUM

// SPI:
// First SPI interface is reserved for an external memory usage
#define EOS_SPI_COUNT SOC_SPI_PERIPH_NUM
#define EOS_SPI_RESERVED {0}

// UART:
#define EOS_UART_COUNT SOC_UART_NUM
#define EOS_UART_RESERVED {0}
