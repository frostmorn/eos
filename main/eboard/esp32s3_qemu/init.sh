#!/bin/ush
echo "=== Booting EOS (ESP32-S3 QEMU) ==="
devctl attach storage flash root
devctl attach bus gpio root
devctl attach display qemu_rgb root --cfg width=800,height=480,bpp=16
launcher &
ush
