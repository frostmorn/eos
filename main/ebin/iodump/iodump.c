#include "ecore/bin.h"
#include <driver/gpio.h>
#include <stdio.h>

int iodump_main(int argc, char **argv){
  gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
  return 0;
}

EOS_BIN_ATTR eos_bin_t iodump_app = {
    EOS_BIN_INITIALIZER, .filename = "iodump", .name = "iodump",
    .entry_point = iodump_main};
