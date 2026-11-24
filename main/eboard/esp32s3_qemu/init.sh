#!/bin/ush
echo "=== Booting EOS (ESP32-S3 QEMU) ==="
devctl attach bus gpio root
devctl attach input gpio_keyboard gpio
devctl attach bus usb root
devctl attach usb serial usb0
devctl attach usb modem usb0
devctl attach storage flash root
mount -a
devctl attach usb msc usb0
devctl attach display qemu_rgb root
devctl attach display compositor fb0 --cfg role=main,x=0,y=32,w=280,h=176,z=1
devctl attach display compositor fb0 --cfg role=status,x=0,y=0,w=280,h=32,z=1
devctl attach display compositor fb0 --cfg role=toolbar,x=0,y=208,w=280,h=32,z=1
devctl attach display compositor fb0 --cfg role=full,x=0,y=0,w=280,h=240,z=10
statusbar &
toolbar &
launcher &
ush
