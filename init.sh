#!/bin/ush
echo "=== Booting EOS (Guition JC4880P443C) ==="
devctl attach display st7701s root --pin rst=5,backlight=23 --cfg width=480,height=800,rotation=0,lane_num=2,bitrate_mbps=750,dpi_clk_mhz=28,ldo_chan=3,ldo_voltage_mv=2500
devctl attach bus i2c root --pin sda=7,scl=8 --cfg port=0,clk_speed=400000
devctl attach input gt911 i2c0 --pin rst=-1,int=-1 --cfg clk_speed=400000,width=480,height=800,addr=0x5D
devctl attach bus gpio root
devctl attach input keyboard gpio --pin b=35 --cfg mode=1,pull=1
devctl attach bus sdio root --pin clk=43,cmd=44,d0=39,d1=40,d2=41,d3=42 --cfg slot=0,width=4,freq_khz=40000,ldo_chan=4
devctl attach storage sd sdio0 --cfg clock_speed_hz=40000000
devctl attach bus sdio root --pin clk=18,cmd=19,d0=14,d1=15,d2=16,d3=17 --cfg slot=1,width=4,freq_khz=40000,lazy_slot=1
devctl attach net wifi sdio1 --pin chip_pu=54
devctl attach net sta wifi0
devctl attach net ap wifi0
launcher &
ush
