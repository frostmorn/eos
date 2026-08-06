#!/usr/bin/bash

FIRMWARE_ELF=$1

# if opeonocd fails, then you've not configured esp-idf environment
openocd -f board/esp32s3-builtin.cfg& 
xtensa-esp32s3-elf-gdb  ${FIRMWARE_ELF} --tui -ex "target extended-remote localhost:3333"
