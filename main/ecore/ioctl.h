#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

//=====================================================
// ioctl call table
// IOCTL CODE, ARGS
//=====================================================
typedef enum {
  //=====================================================
  // any device
  //=====================================================
  EOS_IOCTL_GET_DEV = 10, // eos_dev_t **dev

  EOS_IOCTL_BASE = 100,
  //=====================================================
  // edriver/bus/gpio
  //=====================================================
  EOS_GPIO_IOCTL_SET_FLOATING,         // no args
  EOS_GPIO_IOCTL_SET_PULLUP,           // no args
  EOS_GPIO_IOCTL_SET_PULLDOWN,         // no args
  EOS_GPIO_IOCTL_SET_PULLUPDOWN,       // no args
  EOS_GPIO_IOCTL_SET_SAMPLE_PERIOD_US, // uint32_t period
  // EOS_GPIO_IOCTL_SET_ BUFFERING setvbuf() ?
  //=====================================================
  //=====================================================
  // edriver/bus/spi
  //=====================================================
  EOS_SPI_IOCTL_SET_MODE,           // uint32_t mode (0-3)
                                    // MODE 0: CPOL=0, CPHA=0
                                    // MODE 1: CPOL=0, CPHA=1
                                    // MODE 2: CPOL=1, CPHA=0
                                    // MODE 3: CPOL=1, CPHA=1
  EOS_SPI_IOCTL_SET_CLOCK_HZ,       // uint32_t hz
  EOS_SPI_IOCTL_SET_QUEUE_SIZE,     // uint32_t depth (transaction queue)
  EOS_SPI_IOCTL_SET_COMMAND_BITS,   // uint32_t bits (0-16)
  EOS_SPI_IOCTL_SET_ADDRESS_BITS,   // uint32_t bits (0-64)
  EOS_SPI_IOCTL_SET_DUMMY_BITS,     // uint32_t bits
  EOS_SPI_IOCTL_SET_INPUT_DELAY_NS, // uint32_t ns - MISO timing compensation
  EOS_SPI_IOCTL_SET_BIT_ORDER,      // uint32_t order: 0=MSB first, 1=LSB first
                                    // (applies to both tx and rx)
  EOS_SPI_IOCTL_SET_HALFDUPLEX,     // uint32_t enable (0/1)
  EOS_SPI_IOCTL_SET_3WIRE,          // uint32_t enable (0/1) -
                                    // MOSI/MISO share one line
  EOS_SPI_IOCTL_SET_POSITIVE_CS,    // uint32_t enable (0/1) - CS active-high
                                    // instead of the default active-low
  EOS_SPI_IOCTL_SET_CLK_AS_CS,      // uint32_t enable (0/1) - toggle SCLK as a
                                    // makeshift CS on single-device buses
  EOS_SPI_IOCTL_SET_CS_PRETRANS_US, // uint32_t us - delay between CS assert and
                                    // clock start
  EOS_SPI_IOCTL_SET_CS_POSTTRANS_US, // uint32_t us - delay between clock end
                                     // and CS deassert
  //=====================================================
  //=====================================================
  // edriver/dispay
  //=====================================================
  // Those calls have to return eos_error_t
  EOS_DISPLAY_IOCTL_GET_WIDTH,  // uint32_t *width
  EOS_DISPLAY_IOCTL_GET_HEIGHT, // uint32_t *height

  //=====================================================
  // edriver/storage
  //=====================================================
  EOS_STORAGE_IOCTL_GET_SECTOR_SIZE, // uint32_t *sector_size
  EOS_STORAGE_IOCTL_GET_CAPACITY,    // uint32_t *capacity
  EOS_STORAGE_IOCTL_MOUNT,           // const char *path, bool *result
  EOS_STORAGE_IOCTL_UMOUNT,          // bool *result
} eos_ioctl_t;

// (^__^)==\~
