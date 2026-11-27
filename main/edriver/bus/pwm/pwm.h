#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
// Hardware PWM driver header for LEDC peripheral
// (o_O)==\~

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <driver/ledc.h>

#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

#define EOS_PWM_DEFAULT_FREQ_HZ       5000
#define EOS_PWM_DEFAULT_DUTY_PCT      100
#define EOS_PWM_DUTY_MAX_16BIT        65535
#include "emisc/fancymacro.h"
#define EOS_PWM_RES_BITS_DEFAULT      10
#define EOS_PWM_HW_DUTY_MAX_DEFAULT   1023
#define EOS_PWM_READ_BUF_LEN          32

typedef struct {
  int32_t pin;
  uint32_t freq_hz;
  uint16_t duty16;
  uint8_t duty_pct;
  bool enabled;
  uint32_t polarity;
  int32_t timer_idx;
  int32_t channel_idx;
  ledc_timer_bit_t resolution;
  uint32_t hw_duty_max;
  bool timer_owner;
  int32_t read_offset;
} pwm_dev_state_t;

bool driver_bus_pwm_init(eos_dev_t *dev);
void driver_bus_pwm_shutdown(eos_dev_t *dev);
int driver_bus_pwm_open(eos_dev_t *dev, const char *path, int flags, int mode);
int driver_bus_pwm_close(eos_dev_t *dev, int fd);
int driver_bus_pwm_fstat(eos_dev_t *dev, int fd, struct stat *st);
ssize_t driver_bus_pwm_read(eos_dev_t *dev, int fd, void *buf, size_t count);
ssize_t driver_bus_pwm_write(eos_dev_t *dev, int fd, const void *buf, size_t count);
int driver_bus_pwm_ioctl(eos_dev_t *dev, int fd, int request, va_list args);

extern const eos_drv_t driver_bus_pwm;
