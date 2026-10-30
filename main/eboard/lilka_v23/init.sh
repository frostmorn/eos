#!/bin/ush
echo "=== Booting EOS (Lilka v2.3) ==="
devctl attach storage flash root
devctl attach bus gpio root
devctl attach bus spi root --pin sclk=18,mosi=17,miso=8 --cfg host=1,dma=3
devctl attach bus i2c root --pin sda=13,scl=14 --cfg port=0,clk_speed=400000
devctl attach display st7789 spi0 --pin cs=7,dc=15,en=46 --cfg clock_speed_hz=80000000,rotation=3,width=240,height=280
devctl attach storage sd spi0 --pin cs=16 --cfg clock_speed_hz=20000000
devctl attach input gpio_keyboard gpio --pin up=38,down=41,left=39,right=40,select=0,start=4,a=5,b=6,c=10,d=9 --cfg mode=1,pull=1
devctl attach net wifi root
devctl attach net sta wifi0
devctl attach net ap wifi0
launcher &
ush
