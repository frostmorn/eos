#pragma once
#include "sys/devtree.h"
#include "sys/driver.h"
#include <driver/spi_master.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <hal/adc_types.h>
// Per board specific initialization
extern void eos_board_init();