#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char *guess_device_name(uint16_t addr) {
  if (addr > 0x7F) return NULL;
  static const char *const s_known_i2c_devices[128] = {
      [0x00] = "General Call / Broadcast Address",
      [0x08] = "Custom Device / Serial RAM",
      [0x0A] = "SGTL5000 Audio Codec",
      [0x0B] = "LC709203F Battery Fuel Gauge",
      [0x0C] = "AK8963 / AK8975 / MLX90393 Magnetometer",
      [0x0D] = "MLX90393 Magnetometer / QMC5883L Compass",
      [0x0E] = "MAG3110 / MLX90393 Magnetometer",
      [0x0F] = "MLX90393 Magnetometer",
      [0x10] = "VEML7700 / VEML6075 Light Sensor, PA1010D GPS",
      [0x11] = "Si4713 FM Radio Transmitter",
      [0x12] = "AW9310x Touch Controller / PMSA0031 Particulate Sensor",
      [0x13] = "VCNL40x0 Proximity Sensor",
      [0x14] = "GT911 Touch Controller (alt) / AW9310x Touch",
      [0x15] = "BMA250 / BMA222E 3-Axis Accelerometer",
      [0x16] = "BMA250 / BMA280 Accelerometer",
      [0x17] = "BMA250 / BMA280 Accelerometer",
      [0x18] = "ES8311 / ES8388 Audio Codec, LIS3DH / MCP9808",
      [0x19] = "LIS3DH / LSM303 Accel, MCP9808 Temp",
      [0x1A] = "WM8978 / WM8960 Audio Codec, AGS02MA TVOC Sensor",
      [0x1B] = "MCP9808 Temp Sensor, BMA250",
      [0x1C] = "MMA8452Q / FXOS8700 Accel, LIS3MDL Magnetometer",
      [0x1D] = "ADXL345 / ADXL343 (alt), MMA8451 / FXOS8700",
      [0x1E] = "HMC5883L / LIS3MDL / LIS2MDL Magnetometer",
      [0x1F] = "FXOS8700 Accel/Mag, MCP9808 Temp",
      [0x20] = "MCP23017 / MCP23008 / PCF8574 GPIO Expander",
      [0x21] = "MCP23017 / MCP23008 / PCF8574 GPIO Expander",
      [0x22] = "MCP23017 / PCF8574 GPIO Expander, FUSB302 USB-PD",
      [0x23] = "BH1750 Ambient Light Sensor, MCP23017 / PCF8574",
      [0x24] = "PN532 NFC/RFID Reader, MCP23017 / PCF8574",
      [0x25] = "MCP23017 / PCF8574 GPIO Expander, FUSB302 USB-PD",
      [0x26] = "MSA301 3-Axis Accelerometer, MCP23017 / PCF8574",
      [0x27] = "PCF8574 I2C LCD Backpack / MCP23017 GPIO",
      [0x28] = "BNO055 IMU, CAP1188 Touch, DS3502 Digipot",
      [0x29] = "VL53L0X / VL53L1X ToF, TCS34725 Color, TSL2561",
      [0x2A] = "CAP1188 Touch, SGTL5000 (alt), PCT2075",
      [0x2B] = "CAP1188 Touch, DS3502 Digipot (alt)",
      [0x2C] = "MCP4461 / MCP4561 Digital Potentiometer",
      [0x2D] = "CAP1188 Touch (alt), PCT2075 Temp",
      [0x2E] = "PCT2075 / LM75 Temp Sensor",
      [0x2F] = "PCT2075 Temp, PCA9536 GPIO Expander",
      [0x30] = "SI7021 (alt), ATECC608A Crypto (alt)",
      [0x31] = "Sensirion SDP8xx Flow, NeoKey/NeoSlider",
      [0x32] = "MS5637 / MS5803 / MS5837 Pressure Sensor",
      [0x33] = "MLX90640 Far-Infrared Thermal Camera",
      [0x34] = "AXP192 / AXP2101 Power Management IC (PMU)",
      [0x35] = "AXP2101 / AXP173 PMU (alt)",
      [0x36] = "MAX17048 / MAX17043 LiPo Fuel Gauge",
      [0x37] = "AXP192 / AXP202 (alt), MAX17049 Fuel Gauge",
      [0x38] = "FT6236 / FT6336 Touch, AHT10 / AHT20 Humidity",
      [0x39] = "APDS-9960 Gesture/RGB, AS7341 Spectral Sensor",
      [0x3A] = "MLX90614 Non-Contact IR Thermometer (alt)",
      [0x3B] = "FT6336U Touch (alt)",
      [0x3C] = "SSD1306 / SH1106 / SSD1309 OLED Display (0x3C)",
      [0x3D] = "SSD1306 / SH1106 OLED Display (0x3D)",
      [0x3E] = "LCD1602 / ST7032 Display Controller",
      [0x3F] = "PCF8574A I2C LCD Backpack (0x3F)",
      [0x40] = "INA219 / INA226 Current, SI7021 / HTU21D, PCA9685",
      [0x41] = "INA219 / INA226 (alt), PCA9685 PWM (alt)",
      [0x42] = "INA219 (alt), NEO-M8 / u-blox GNSS/GPS",
      [0x43] = "INA219 / INA226 (alt), PCA9685 (alt)",
      [0x44] = "SHT30 / SHT31 / SHT35 / SHT40 Temp & Humidity",
      [0x45] = "SHT30 / SHT31 (alt), INA219 (alt)",
      [0x46] = "INA219 / INA260 (alt), TMP006 / TMP007 IR Temp",
      [0x47] = "INA219 / INA260 (alt), TMP006 / TMP007 IR Temp",
      [0x48] = "ADS1115 / ADS1015 ADC, TMP102 / LM75 / PCF8591",
      [0x49] = "ADS1115 (alt), TMP102 (alt), TSL2561 (alt)",
      [0x4A] = "ADS1115 (alt), MAX30100 / MAX30102 Oximeter",
      [0x4B] = "ADS1115 (alt), TMP102 (alt), BNO080 IMU (alt)",
      [0x4C] = "EMC2101 Fan Controller, PCF8591 (alt)",
      [0x4D] = "PCF8591 ADC/DAC (alt), INA219 / INA260 (alt)",
      [0x4E] = "PCF8591 ADC/DAC (alt), INA219 / INA260 (alt)",
      [0x4F] = "PCF8591 ADC/DAC (alt), INA219 / INA260 (alt)",
      [0x50] = "AT24C32 / AT24Cxx EEPROM, MB85RC FRAM",
      [0x51] = "PCF8563 / BM8563 RTC, AT24Cxx EEPROM",
      [0x52] = "BM8563 RTC (alt), ENS160 Air Quality Sensor",
      [0x53] = "ADXL345 3-Axis Accelerometer, ENS160, AT24Cxx",
      [0x54] = "AT24Cxx EEPROM (0x54)",
      [0x55] = "BQ27441 LiPo Fuel Gauge, AT24Cxx EEPROM",
      [0x56] = "AT24Cxx EEPROM (0x56)",
      [0x57] = "AT24C32 EEPROM (RTC module), MAX30102 Oximeter",
      [0x58] = "SGP30 / SGP40 Air Quality, TPA2016D2 Audio Amp",
      [0x59] = "SGP40 Air Quality (alt)",
      [0x5A] = "MPR121 Touch, CCS811 VOC, DRV2605 Haptic, MLX90614",
      [0x5B] = "MPR121 Touch (alt), CCS811 VOC (alt)",
      [0x5C] = "AM2320 / DHT12 Temp/Humidity, BH1750 Light (alt)",
      [0x5D] = "GT911 / GT9271 Touch Controller, LPS22HB Pressure",
      [0x5E] = "TLV493D 3-Axis Magnetometer",
      [0x5F] = "HTS221 Humidity/Temp Sensor",
      [0x60] = "Si5351A Clock Generator, MCP4725 DAC, ATECC608A",
      [0x61] = "MCP4725 DAC (alt), SCD30 CO2 Sensor",
      [0x62] = "SCD40 / SCD41 CO2 Sensor, MCP4725 (alt)",
      [0x63] = "MCP4725 DAC (alt), Si4713 FM Transmitter (alt)",
      [0x64] = "MCP4725A2 DAC (alt), MCP9600 Thermocouple",
      [0x65] = "MCP4725A2 DAC (alt), MCP9600 Thermocouple",
      [0x66] = "MCP4725A3 DAC (alt), MCP9600 Thermocouple",
      [0x67] = "MCP4725A3 DAC (alt), MCP9600 Thermocouple",
      [0x68] = "MPU-6050 / MPU-9250 IMU, DS3231 / DS1307 RTC",
      [0x69] = "MPU-6050 / MPU-9250 (alt), AMG8833 Thermal Camera",
      [0x6A] = "LSM6DS3 / LSM6DSO / LSM6DSL 6-Axis IMU",
      [0x6B] = "LSM6DS3 / LSM6DSO (alt), BQ24195 Battery Charger",
      [0x6C] = "MAX17055 / MAX17260 LiPo Fuel Gauge",
      [0x6D] = "XGZP6897D / XGZP Pressure Sensor",
      [0x6E] = "MAX17055 Fuel Gauge (alt)",
      [0x6F] = "MCP7940N / MCP79410 RTC / EEPROM",
      [0x70] = "TCA9548A / PCA9548A Multiplexer, HT16K33, SHTC3",
      [0x71] = "TCA9548A Multiplexer (0x71), HT16K33 (alt)",
      [0x72] = "TCA9548A Multiplexer (0x72), HT16K33 (alt)",
      [0x73] = "TCA9548A Multiplexer (0x73), HT16K33 (alt)",
      [0x74] = "TCA9548A Multiplexer (0x74), IS31FL3731 LED Driver",
      [0x75] = "TCA9548A Multiplexer (0x75), IS31FL3731 LED Driver",
      [0x76] = "BMP280 / BME280 / BME680 Sensor, MS5611",
      [0x77] = "BMP280 / BME280 / BME680 (alt), BMP180 Pressure",
  };
  return s_known_i2c_devices[addr];
}

static int scan_i2c_bus(const char *bus_path) {
  int fd = open(bus_path, O_RDWR);
  if (fd < 0) {
    printf("[ERROR] Cannot open %s: %s\n", bus_path, strerror(errno));
    return -1;
  }

  printf("\n====================================================\n");
  printf(" Scanning I2C bus: %s\n", bus_path);
  int sda = -1, scl = -1;
  if (ioctl(fd, EOS_I2C_IOCTL_GET_PINS, &sda, &scl) == 0) {
    if (sda >= 0 && scl >= 0) {
      printf(" Pins: SDA=GPIO%d, SCL=GPIO%d\n", sda, scl);
    } else {
      printf(" Pins: SDA=%d, SCL=%d (unconfigured)\n", sda, scl);
    }
  }
  printf("====================================================\n");
  printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

  uint16_t found_addrs[128];
  size_t found_count = 0;

  for (int row = 0; row < 128; row += 16) {
    printf("%02x: ", row);
    for (int col = 0; col < 16; col++) {
      uint16_t addr = (uint16_t)(row + col);
      if (addr < 0x03 || addr > 0x77) {
        printf("   ");
      } else {
        int rc = ioctl(fd, EOS_I2C_IOCTL_PROBE, addr, 20);
        if (rc == 0) {
          printf("%02x ", (unsigned)addr);
          if (found_count < sizeof(found_addrs) / sizeof(found_addrs[0])) {
            found_addrs[found_count++] = addr;
          }
        } else {
          printf("-- ");
        }
      }
    }
    printf("\n");
  }

  printf("----------------------------------------------------\n");
  if (found_count == 0) {
    printf("Result: No devices detected on %s\n", bus_path);
  } else {
    printf("Result: Found %zu device(s) on %s:\n", found_count, bus_path);
    for (size_t i = 0; i < found_count; i++) {
      uint16_t a = found_addrs[i];
      const char *guess = guess_device_name(a);
      if (guess) {
        printf("  - 0x%02X (%3u): %s\n", (unsigned)a, (unsigned)a, guess);
      } else {
        printf("  - 0x%02X (%3u)\n", (unsigned)a, (unsigned)a);
      }
    }
  }

  // Also verify directory entries exposed by devfs for this bus
  DIR *dir = opendir(bus_path);
  if (dir) {
    printf("\nDevice nodes in %s:\n", bus_path);
    struct dirent *ent;
    size_t node_count = 0;
    while ((ent = readdir(dir)) != NULL) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        continue;
      printf("  %s/%s\n", bus_path, ent->d_name);
      node_count++;
    }
    if (node_count == 0) {
      printf("  (none)\n");
    }
    closedir(dir);
  }

  close(fd);
  return 0;
}

int lsi2c_main(int argc, char **argv) {
  if (argc > 1 &&
      (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    printf("Usage: lsi2c [options] [bus_path ...]\n");
    printf("Scan and list connected I2C devices across system buses.\n\n");
    printf("Options:\n");
    printf("  -p, --pins <sda> <scl> [bus]  Reconfigure I2C pins (-1 to release)\n");
    printf("  -h, --help                    Show this help message\n\n");
    printf("Examples:\n");
    printf("  lsi2c                         # Discover and scan all I2C buses\n");
    printf("  lsi2c /dev/i2c                # Scan /dev/i2c specifically\n");
    printf("  lsi2c -p 10 11 /dev/i2c       # Reconfigure /dev/i2c pins to SDA=10, SCL=11\n");
    printf("  lsi2c -p -1 -1 /dev/i2c       # Release pins and restore GPIO function\n");
    return 0;
  }

  if (argc >= 4 &&
      (strcmp(argv[1], "-p") == 0 || strcmp(argv[1], "--pins") == 0)) {
    int sda = atoi(argv[2]);
    int scl = atoi(argv[3]);
    const char *bus_path = (argc >= 5) ? argv[4] : "/dev/i2c";
    int fd = open(bus_path, O_RDWR);
    if (fd < 0) {
      printf("[ERROR] Cannot open %s: %s\n", bus_path, strerror(errno));
      return -1;
    }
    printf("Reconfiguring %s pins: SDA=%d, SCL=%d...\n", bus_path, sda, scl);
    if (ioctl(fd, EOS_I2C_IOCTL_SET_PINS, sda, scl) != 0) {
      if (errno == EBUSY) {
        printf("[ERROR] Cannot reconfigure pins: devices are attached to this I2C bus\n");
      } else {
        printf("[ERROR] Failed to set pins: %s\n", strerror(errno));
      }
      close(fd);
      return -1;
    }
    printf("Successfully updated pin configuration.\n");
    if (sda < 0 || scl < 0) {
      printf("Pins released and restored to standard GPIO function.\n");
      close(fd);
      return 0;
    }
    close(fd);
    scan_i2c_bus(bus_path);
    return 0;
  }

  if (argc > 1) {
    for (int i = 1; i < argc; i++) {
      scan_i2c_bus(argv[i]);
    }
    return 0;
  }

  // Discover all I2C buses from /dev
  char bus_list[8][300];
  size_t bus_count = 0;

  DIR *dev_dir = opendir("/dev");
  if (dev_dir) {
    struct dirent *ent;
    while ((ent = readdir(dev_dir)) != NULL) {
      if (strncmp(ent->d_name, "i2c", 3) == 0) {
        char fullpath[300];
        snprintf(fullpath, sizeof(fullpath), "/dev/%s", ent->d_name);
        bool duplicate = false;
        for (size_t b = 0; b < bus_count; b++) {
          if (strcmp(bus_list[b], fullpath) == 0) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate && bus_count < sizeof(bus_list) / sizeof(bus_list[0])) {
          strlcpy(bus_list[bus_count++], fullpath, sizeof(bus_list[0]));
        }
      }
    }
    closedir(dev_dir);
  }

  // Fallbacks if /dev listing is empty
  if (bus_count == 0) {
    if (access("/dev/i2c", F_OK) == 0) {
      strlcpy(bus_list[bus_count++], "/dev/i2c", sizeof(bus_list[0]));
    } else if (access("/dev/i2c0", F_OK) == 0) {
      strlcpy(bus_list[bus_count++], "/dev/i2c0", sizeof(bus_list[0]));
    }
  }

  if (bus_count == 0) {
    printf("No I2C buses found in the system (/dev/i2c not present).\n");
    return -1;
  }

  for (size_t i = 0; i < bus_count; i++) {
    scan_i2c_bus(bus_list[i]);
  }

  return 0;
}

EOS_BIN_ATTR const eos_bin_t lsi2c_app = {
    EOS_BIN_INITIALIZER,
    .filename = "lsi2c",
    .name = "List I2C Devices",
    .group = "Utils",
    .description = "Scan system I2C buses and detect connected device addresses",
    .entry_point = lsi2c_main,
};
