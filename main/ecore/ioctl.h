#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include <stddef.h>
#include <stdint.h>
#include "ecore/netif.h"

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
} eos_display_rect_t;

typedef struct {
  uint16_t addr;          // 7-bit slave address
  const void *tx_buf;     // Buffer of bytes to write (NULL if receive only)
  size_t tx_len;          // Number of bytes to write
  void *rx_buf;           // Buffer for received bytes (NULL if transmit only)
  size_t rx_len;          // Number of bytes to read
  uint32_t clk_speed_hz;  // Optional clock frequency override (0 = default)
  int timeout_ms;         // Timeout in milliseconds (-1 for default)
} eos_i2c_msg_t;

typedef struct {
  uint8_t addrs[128];     // Discovered 7-bit slave addresses
  size_t count;           // Number of discovered devices
  int timeout_ms;         // Timeout per address probe in milliseconds
} eos_i2c_scan_t;

typedef struct {
  uint32_t function;
  uint32_t reg;
  uint8_t write_val;
  uint8_t read_val;
} eos_sdio_cmd52_t;

typedef struct {
  uint32_t function;
  uint32_t addr;
  void *buf;
  size_t len;
  bool fixed_addr;
} eos_sdio_cmd53_t;

typedef struct {
  uint32_t sample_rate;    // e.g. 44100
  uint8_t bits_per_sample; // e.g. 16, 24, 32
  uint8_t channels;        // 1 (mono) or 2 (stereo)
  uint8_t volume;          // 0 - 100
  uint8_t mult;            // 1 - 100 (multiplier %, default 10 for earplugs mode)
  uint16_t delay_ms;       // Buffer delay/latency in milliseconds (default 60 ms)
} eos_sound_config_t;

// Sound driver performance and time shift statistics
typedef struct {
  uint32_t total_writes;       // Total number of write calls in stream
  uint64_t total_bytes;        // Total audio payload bytes written
  uint64_t total_samples;      // Total audio samples written
  uint32_t underruns;          // Number of buffer underruns detected
  int64_t stream_start_us;     // Microsecond timestamp of stream start
  int64_t last_write_us;       // Microsecond timestamp of most recent write
  int64_t stream_audio_us;     // Total theoretical audio duration in microseconds
  int64_t stream_wall_us;      // Wall-clock microseconds elapsed since stream start
  int64_t total_lock_us;       // Total microseconds spent waiting for driver lock
  int64_t total_proc_us;       // Total microseconds spent doing volume/math processing
  int64_t total_i2s_wait_us;   // Total microseconds spent blocked inside i2s_channel_write
  int32_t min_i2s_wait_us;     // Minimum single i2s_channel_write blocking duration (us)
  int32_t max_i2s_wait_us;     // Maximum single i2s_channel_write blocking duration (us)
  int32_t last_lead_us;        // Instantaneous pipeline lead / time shift (stream_audio - stream_wall)
  uint32_t bclk_actual_hz;     // Actual hardware BCLK frequency reported by I2S controller
  uint32_t dma_desc_num;       // Number of DMA descriptors
  uint32_t dma_frame_num;      // Frames per DMA descriptor
} eos_sound_stats_t;

// ADC reading structure for EOS_ADC_IOCTL_READ_RAW / READ_VOLTAGE
typedef struct {
  uint32_t pin;         // GPIO pin number (e.g. 3)
  int32_t  raw;         // Raw 12-bit ADC conversion value (0 - 4095)
  int32_t  voltage_mv;  // Factory-calibrated voltage in millivolts
} eos_adc_reading_t;

// Battery status enumeration
typedef enum {
  EOS_BATTERY_STATUS_NOT_PRESENT = 0,
  EOS_BATTERY_STATUS_DISCHARGING = 1,
  EOS_BATTERY_STATUS_CHARGING    = 2,
  EOS_BATTERY_STATUS_FULL        = 3
} eos_battery_status_t;

// Battery information structure for EOS_POWER_IOCTL_GET_BATTERY_INFO
typedef struct {
  uint32_t voltage_mv;         // Measured/smoothed battery cell voltage in mV
  int32_t  soc_percent;        // State of Charge (0 - 100%, or -1 if not connected)
  eos_battery_status_t status; // Battery operational status
  uint32_t v_empty_mv;         // Nominal empty threshold in mV (e.g. 3200)
  uint32_t v_full_mv;          // Nominal full threshold in mV (e.g. 4200)
} eos_battery_info_t;
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
  // Pin configuration:
  EOS_GPIO_IOCTL_SET_FLOATING,         // no args
  EOS_GPIO_IOCTL_SET_PULLUP,           // no args
  EOS_GPIO_IOCTL_SET_PULLDOWN,         // no args
  EOS_GPIO_IOCTL_SET_PULLUPDOWN,       // no args
  // IO:
  EOS_GPIO_IOCTL_SET_IOMODE_BIN,       // no args 1/0
  EOS_GPIO_IOCTL_SET_IOMODE_CHAR,      // no args '1'/'0'
  // Sampling:
  EOS_GPIO_IOCTL_SET_SAMPLE_PERIOD_US, // uint32_t period

  // EOS_GPIO_IOCTL_SET_ BUFFERING setvbuf() ?
  //=====================================================
  //=====================================================
  // edriver/bus/spi
  //=====================================================
  // spi role:
  EOS_SPI_IOCTL_SET_ROLE_MASTER,    // no args
  EOS_SPI_IOCTL_SET_ROLE_SLAVE,
  // spi pins:
  EOS_SPI_IOCTL_SET_MOSI,           // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_MISO,           // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_SCLK,           // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA0,          // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA1,          // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA2,          // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA3,          // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA4,          // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA5,          // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA6,          // int pinNo, -1 if unused
  EOS_SPI_IOCTL_SET_DATA7,          // int pinNo, -1 if unused
  //-----------------------------------------------------
  EOS_SPI_IOCTL_SET_MODE,           // uint32_t mode (0-3)
                                    // MODE 0: CPOL=0, CPHA=0 [DEFAULT]
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
  // edriver/bus/i2c
  //=====================================================
  EOS_I2C_IOCTL_TRANSFER,         // eos_i2c_msg_t *msg
  EOS_I2C_IOCTL_TRANSMIT,         // uint16_t addr, const void *buf, size_t len, int timeout_ms
  EOS_I2C_IOCTL_RECEIVE,          // uint16_t addr, void *buf, size_t len, int timeout_ms
  EOS_I2C_IOCTL_WRITE_READ,       // uint16_t addr, const void *tx, size_t tx_len, void *rx, size_t rx_len, int timeout_ms
  EOS_I2C_IOCTL_PROBE,            // uint16_t addr, int timeout_ms -> 0 if ACK, -1 if NACK/err
  EOS_I2C_IOCTL_SCAN,             // eos_i2c_scan_t *scan
  EOS_I2C_IOCTL_SET_FREQ,         // uint32_t clk_speed_hz
  EOS_I2C_IOCTL_GET_BUS_HANDLE,   // void **out_handle (i2c_master_bus_handle_t)
  EOS_I2C_IOCTL_SET_SLAVE_ADDR,   // uint16_t addr (sets target slave addr for fd read/write)
  EOS_I2C_IOCTL_SET_SDA,          // int pinNo (-1 to release)
  EOS_I2C_IOCTL_SET_SCL,          // int pinNo (-1 to release)
  EOS_I2C_IOCTL_SET_PINS,         // int sda_pin, int scl_pin (-1 to release)
  EOS_I2C_IOCTL_GET_PINS,         // int *out_sda, int *out_scl

  //=====================================================
  // edriver/bus/sdio
  //=====================================================
  EOS_SDIO_IOCTL_GET_HOST,        // void * (sdmmc_host_t *)
  EOS_SDIO_IOCTL_GET_SLOT,        // int *slot
  EOS_SDIO_IOCTL_SET_CLOCK_HZ,    // uint32_t freq_hz
  EOS_SDIO_IOCTL_SET_BUS_WIDTH,   // uint32_t width (1, 4, 8)
  EOS_SDIO_IOCTL_RESET,           // no args
  EOS_SDIO_IOCTL_IO_READ_BYTE,    // eos_sdio_cmd52_t *cmd
  EOS_SDIO_IOCTL_IO_WRITE_BYTE,   // eos_sdio_cmd52_t *cmd
  EOS_SDIO_IOCTL_IO_READ_BYTES,   // eos_sdio_cmd53_t *cmd
  EOS_SDIO_IOCTL_IO_WRITE_BYTES,  // eos_sdio_cmd53_t *cmd
  EOS_SDIO_IOCTL_GET_CARD,        // void ** (sdmmc_card_t **)
  EOS_SDIO_IOCTL_PROBE_CARD,      // void * (sdmmc_card_t *)

  //=====================================================
  // edriver/dispay
  //=====================================================
  // Those calls have to return int (0 on success, -1 on error with errno set)
  EOS_DISPLAY_IOCTL_GET_WIDTH,  // uint32_t *width
  EOS_DISPLAY_IOCTL_GET_HEIGHT, // uint32_t *height
  EOS_DISPLAY_IOCTL_SET_WINDOW, // const eos_display_rect_t *rect

  //=====================================================
  // edriver/storage
  //=====================================================
  EOS_STORAGE_IOCTL_GET_SECTOR_SIZE, // uint32_t *sector_size
  EOS_STORAGE_IOCTL_GET_CAPACITY,    // uint32_t *capacity
  EOS_STORAGE_IOCTL_MOUNT,           // const char *path, bool *result
  EOS_STORAGE_IOCTL_UMOUNT,          // bool *result

  //=====================================================
  // edriver/input/keyboard
  //=====================================================
  EOS_KEYBOARD_IOCTL_POLL,           // eos_input_event_t *ev

  //=====================================================
  // edriver/input/touch
  //=====================================================
  EOS_TOUCH_IOCTL_POLL,              // eos_input_event_t *ev
  EOS_TOUCH_IOCTL_GET_COORDS,        // uint16_t *x, uint16_t *y, bool *pressed
  EOS_TOUCH_IOCTL_GET_PRESSURE,      // uint16_t *pressure

  //=====================================================
  // edriver/sound
  //=====================================================
  EOS_SOUND_IOCTL_GET_CONFIG = 400,     // eos_sound_config_t *out_cfg
  EOS_SOUND_IOCTL_SET_CONFIG,          // const eos_sound_config_t *cfg
  EOS_SOUND_IOCTL_SET_SAMPLE_RATE,     // uint32_t rate_hz
  EOS_SOUND_IOCTL_GET_SAMPLE_RATE,     // uint32_t *out_rate_hz
  EOS_SOUND_IOCTL_SET_BITS_PER_SAMPLE, // uint32_t bits
  EOS_SOUND_IOCTL_GET_BITS_PER_SAMPLE, // uint32_t *out_bits
  EOS_SOUND_IOCTL_SET_CHANNELS,        // uint32_t channels
  EOS_SOUND_IOCTL_GET_CHANNELS,        // uint32_t *out_channels
  EOS_SOUND_IOCTL_SET_VOLUME,          // uint32_t volume (0 - 100)
  EOS_SOUND_IOCTL_GET_VOLUME,          // uint32_t *out_volume
  EOS_SOUND_IOCTL_SET_MULT,            // uint32_t mult (1 - 100, 10 = earplugs 10% limit)
  EOS_SOUND_IOCTL_GET_MULT,            // uint32_t *out_mult
  EOS_SOUND_IOCTL_START,               // no args
  EOS_SOUND_IOCTL_STOP,                // no args
  EOS_SOUND_IOCTL_GET_DELAY,           // uint32_t *out_delay_ms
  EOS_SOUND_IOCTL_SET_DELAY,           // uint32_t delay_ms
  EOS_SOUND_IOCTL_GET_STATS,           // eos_sound_stats_t *out_stats

  //=====================================================
  // edriver/net
  //=====================================================
  // Common netif operations:
  EOS_NETIF_IOCTL_GET_INFO = 500,    // eos_netif_info_t *info
  EOS_NETIF_IOCTL_SET_UP,            // no args
  EOS_NETIF_IOCTL_SET_DOWN,          // no args
  EOS_NETIF_IOCTL_SET_IP,            // const eos_netif_ip_info_t *ip_info
  EOS_NETIF_IOCTL_GET_IP,            // eos_netif_ip_info_t *ip_info
  EOS_NETIF_IOCTL_START_DHCPC,       // no args
  EOS_NETIF_IOCTL_STOP_DHCPC,        // no args
  EOS_NETIF_IOCTL_START_DHCPS,       // no args
  EOS_NETIF_IOCTL_STOP_DHCPS,        // no args
  EOS_NETIF_IOCTL_SET_DNS,           // int type (0=main, 1=backup), const char *dns_ip
  EOS_NETIF_IOCTL_GET_DNS,           // int type, char *dns_ip
  EOS_NETIF_IOCTL_SET_HOSTNAME,      // const char *hostname
  EOS_NETIF_IOCTL_GET_HOSTNAME,      // char *hostname (min 32 bytes)
  EOS_NETIF_IOCTL_SET_MAC,           // const uint8_t mac[6]
  EOS_NETIF_IOCTL_GET_MAC,           // uint8_t mac[6]
  EOS_NETIF_IOCTL_BRIDGE_ADD_IF,     // const char *ifname
  EOS_NETIF_IOCTL_BRIDGE_DEL_IF,     // const char *ifname
  EOS_NETIF_IOCTL_BRIDGE_GET_INFO,   // eos_netif_bridge_info_t *info
  EOS_NETIF_IOCTL_BRIDGE_SET_NAPT,   // bool enable

  // STA-specific operations:
  EOS_STA_IOCTL_CONNECT,             // const eos_sta_connect_t *conn
  EOS_STA_IOCTL_DISCONNECT,          // no args
  EOS_STA_IOCTL_GET_AP_INFO,         // eos_sta_ap_info_t *ap_info
  EOS_STA_IOCTL_SET_AUTOCONNECT,     // bool enable

  // AP-specific operations:
  EOS_AP_IOCTL_SET_CONFIG,           // const eos_ap_config_t *cfg
  EOS_AP_IOCTL_GET_CONFIG,           // eos_ap_config_t *cfg
  EOS_AP_IOCTL_GET_STAS,             // eos_ap_sta_list_t *list

  // Physical Radio operations:
  EOS_RADIO_IOCTL_POWER,             // int state (1 = on, 0 = off)
  EOS_RADIO_IOCTL_GET_POWER,         // int *state
  EOS_RADIO_IOCTL_SET_CHANNEL,       // uint8_t channel
  EOS_RADIO_IOCTL_GET_CHANNEL,       // uint8_t *channel
  EOS_RADIO_IOCTL_SET_TX_POWER,      // int8_t power_dbm
  EOS_RADIO_IOCTL_GET_TX_POWER,      // int8_t *power_dbm
  EOS_RADIO_IOCTL_SET_COUNTRY,       // const char country[3]
  EOS_RADIO_IOCTL_GET_COUNTRY,       // char country[3]
  EOS_RADIO_IOCTL_SCAN_START,        // const eos_wifi_scan_config_t *scan_cfg
  EOS_RADIO_IOCTL_SCAN_GET_COUNT,    // uint16_t *count
  EOS_RADIO_IOCTL_SCAN_GET_RESULTS,  // eos_wifi_scan_results_t *results
  EOS_RADIO_IOCTL_SET_PROMISC,       // const eos_wifi_promisc_cfg_t *cfg
  EOS_RADIO_IOCTL_GET_PROMISC,       // eos_wifi_promisc_cfg_t *cfg

  // Wi-Fi core driver operations (called on wifi0 / dev->parent):
  EOS_WIFI_IOCTL_ENSURE_INIT = 540,  // no args - initializes NVS, netif & Wi-Fi
  EOS_WIFI_IOCTL_SET_MODE,           // int if_type (0=STA, 1=AP), bool enable
  EOS_WIFI_IOCTL_IS_RADIO_ON,        // bool *out_on
  EOS_WIFI_IOCTL_WAIT_CONNECT,       // uint32_t timeout_ms
  EOS_WIFI_IOCTL_GET_CONNECTED_INFO, // eos_sta_ap_info_t *out_info
  EOS_WIFI_IOCTL_SET_CONNECTED_INFO, // const eos_sta_ap_info_t *info
  EOS_WIFI_IOCTL_IS_CONNECTED,       // bool *out_connected
  EOS_WIFI_IOCTL_SET_NAPT,           // bool enable

  //=====================================================
  // edriver/bus/adc
  //=====================================================
  EOS_ADC_IOCTL_READ_RAW = 600,       // eos_adc_reading_t *reading (in: pin, out: raw)
  EOS_ADC_IOCTL_READ_VOLTAGE,        // eos_adc_reading_t *reading (in: pin, out: raw, voltage_mv)

  //=====================================================
  // edriver/power/battery
  //=====================================================
  EOS_POWER_IOCTL_GET_BATTERY_INFO = 650, // eos_battery_info_t *out_info
} eos_ioctl_t;

// (^__^)==\~
