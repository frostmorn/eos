#include "ecore/app.h"
#include <driver/gpio.h>
#include <stdio.h>

int iodump_main(int argc, char **argv){
  gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t iodump_app = {
    EOS_NATIVE_APP_INIT, .filename = "iodump", .name = "iodump",
    .entry_point = iodump_main};
