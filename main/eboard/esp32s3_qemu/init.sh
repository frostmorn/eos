#!/bin/ush
echo "=== Booting EOS (ESP32-S3 QEMU) ==="
devctl attach storage flash root
devctl attach bus gpio root
devctl attach input gpio_keyboard gpio
devctl attach display qemu_rgb root
launcher &
ush
