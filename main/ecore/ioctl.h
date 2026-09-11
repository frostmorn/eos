#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

//=====================================================
// ioctl call table
// IOCTL CODE, ARGS
//=====================================================
typedef enum{
//=====================================================
// any device
//=====================================================
EOS_IOCTL_GET_DEV=10,               // eos_dev_t **dev


EOS_IOCTL_BASE = 100,
//=====================================================
// edriver/bus/gpio
//=====================================================
EOS_GPIO_IOCTL_SET_FLOATING,        // no args 
EOS_GPIO_IOCTL_SET_PULLUP,          // no args
EOS_GPIO_IOCTL_SET_PULLDOWN,        // no args
EOS_GPIO_IOCTL_SET_PULLUPDOWN,      // no args
EOS_GPIO_IOCTL_SET_SAMPLE_PERIOD_US,// uint32_t period
//EOS_GPIO_IOCTL_SET_ BUFFERING setvbuf() ?
//=====================================================
//=====================================================
// edriver/bus/spi
//=====================================================
/*
  MODE 0: CPOL=0, CPHA=0
  MODE 1: CPOL=0, CPHA=1
  MODE 2: CPOL=1, CPHA=0
  MODE 3: CPOL=1, CPHA=1
*/
EOS_SPI_IOCTL_SET_MODE,             // uint32_t mode

//=====================================================
// edriver/dispay
//=====================================================
// Those calls have to return eos_error_t
EOS_DISPLAY_IOCTL_GET_WIDTH,        // uint32_t *width
EOS_DISPLAY_IOCTL_GET_HEIGHT,       // uint32_t *height

//=====================================================
// edriver/storage
//=====================================================
EOS_STORAGE_IOCTL_GET_SECTOR_SIZE,  // uint32_t *sector_size
EOS_STORAGE_IOCTL_GET_CAPACITY,     // uint32_t *capacity
EOS_STORAGE_IOCTL_MOUNT,            // const char *path, bool *result
EOS_STORAGE_IOCTL_UMOUNT,           // bool *result
} eos_ioctl_t;

// (^__^)==\~
