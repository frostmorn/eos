#include "includes.h"
#ifdef EOS_DRV_BUS_GPIO_ENABLED
///////////////////////////////////////////////////////////////////////
// GPIO DRIVER FOR EOS
///////////////////////////////////////////////////////////////////////
// /dev/gpio/X where X is pin no
//
// GPIO bus exposes a list of files /dev/gpio/X where X is a pin no with
// ability to write 1 or 0
///////////////////////////////////////////////////////////////////////

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include <dirent.h>
#include <driver/gpio.h>
#include <errno.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <soc/soc_caps.h>

#define IS_VALID_FD(FD) (!((FD < 0) || (FD >= SOC_GPIO_PIN_COUNT)))

///////////////////////////////////////////////////////////////////////
// DRIVER DATA:
///////////////////////////////////////////////////////////////////////
typedef struct {
  TaskHandle_t task_owner;   // xTaskGetCurrentTaskHandle [can be NULL]
  int32_t sample_period_us;  // count of microseconds till next read
  int64_t last_sample;       // time mark when last sample has been made
  gpio_config_t cfg;         // pin configuration derived from idf
} gpio_pin_t;
//=====================================================================
typedef struct {
  uint32_t esp_idf_fs_index;
  int idx;                   // also gpioNum
} gpio_dir_t;
//=====================================================================
// GPIO count is always constant, meaning if we want to have a blocking
// operation on a pin, we no need more fds than pin count iself, which is,
// useful
typedef struct {
  gpio_pin_t pins[SOC_GPIO_PIN_COUNT];
  // pin direction? in/out
} gpio_bus_state_t;
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// DRIVER IMPLEMENTATION:
///////////////////////////////////////////////////////////////////////
//=====================================================================
bool driver_bus_gpio_init(eos_dev_t *dev) {
  // GPIO INIT
  dev->state = malloc(sizeof(gpio_bus_state_t));
  memset(dev->state, 0, sizeof(gpio_bus_state_t));

  gpio_bus_state_t *state = dev->state;

  for (int i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
    // Skip claimed pins
    if (!eos_cap_is_free(EOS_CAPS_GPIO, i))
      continue;
    // Setup defult pin modes for each gpio

    state->pins[i].cfg.pin_bit_mask = BIT64(i);
    state->pins[i].cfg.mode = GPIO_MODE_INPUT; // safest mode ever
    // GPIO_MODE_DISABLE,
    // GPIO_MODE_INPUT,
    // GPIO_MODE_OUTPUT,
    // GPIO_MODE_OUTPUT_OD,
    // GPIO_MODE_INPUT_OUTPUT_OD,
    // GPIO_MODE_INPUT_OUTPUT,
    state->pins[i].cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    // GPIO_PULLUP_ENABLE,
    state->pins[i].cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // GPIO_PULLDOWN_ENABLE,
    state->pins[i].cfg.intr_type = GPIO_INTR_DISABLE;
    // GPIO_INTR_POSEDGE,
    // GPIO_INTR_NEGEDGE,
    // GPIO_INTR_ANYEDGE,
    // GPIO_INTR_LOW_LEVEL,
    // GPIO_INTR_HIGH_LEVEL,
    // TODO: there's an option to control hysteresis in that struct
    // but it's somehow hardware dependent

    // This thing can blow up right in a face in case something wrong
    // gpio_config(&state->pins[i].cfg);
  }

  return true;
}
//=====================================================================
void driver_bus_gpio_shutdown(eos_dev_t *dev) {
  // GPIO Cleanup
  free(dev->state);
}
//=====================================================================
int driver_bus_gpio_open(eos_dev_t *dev, const char *path, int flags,
                         int mode) {
  // EOS_LOGI("GPIO OPEN: path='%s' flags=%d mode=%d\n", path ? path : "(null)",
  //          flags, mode);
  // Duplicate
  char *gPath = strdup(path);

  // Find last Path token
  char *lastToken = strrchr(path, '/');

  if (lastToken)
    lastToken++;
  else
    lastToken = (char *)path;

  // Retrieve gpio number
  int gpioNum = atoi(lastToken);

  // Ensure zero if zero
  if (gpioNum == 0 && (strcmp(lastToken, "0") != 0)) {
    errno = EBADF;
    free(gPath);
    return -1;
  }

  // Ensure GPIO range
  if (!IS_VALID_FD(gpioNum)) {
    errno = EINVAL;
    free(gPath);
    return -1;
  }

  // Claiming pin
  if (!eos_cap_claim(EOS_CAPS_GPIO, gpioNum, dev)) {
    errno = EBUSY;
    free(gPath);
    return -1;
  }

  // Trying to open GPIO
  gpio_bus_state_t *state = dev->state;
  if (state->pins[gpioNum].task_owner == NULL)
    state->pins[gpioNum].task_owner = xTaskGetCurrentTaskHandle();
  else
    gpioNum = -1; // also fd no [Already Opened]

  // Cleanup
  free(gPath);

  return gpioNum;
}
//=====================================================================
int driver_bus_gpio_close(eos_dev_t *dev, int fd) {
  // Ensure GPIO range
  if (!IS_VALID_FD(fd)) {
    errno = EINVAL;
    return -1;
  }

  // Release pin
  eos_cap_release(EOS_CAPS_GPIO, fd, dev);

  // Free pin related data
  gpio_bus_state_t *state = dev->state;
  state->pins[fd].task_owner = NULL;

  return 0;
}
//=====================================================================
ssize_t driver_bus_gpio_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  if (!IS_VALID_FD(fd)) {
    errno = EINVAL;
    return -1;
  }

  if (!dst && size) {
    errno = EFAULT;
    return -1;
  }
  // TODO: allow to read as '1' character
  gpio_pin_t *pin = &((gpio_bus_state_t *)dev->state)->pins[fd];

  if (pin->cfg.mode != GPIO_MODE_INPUT) {
    pin->cfg.mode = GPIO_MODE_INPUT;

    if (gpio_config(&pin->cfg) != ESP_OK) {
      errno = EIO;
      return -1;
    }
  }

  uint8_t *p = dst;

  for (size_t i = 0; i < size; i++) {
    if (pin->sample_period_us > 0 && pin->last_sample > 0) {
      int64_t elapsed = esp_timer_get_time() - pin->last_sample;

      if (elapsed < pin->sample_period_us) {
        int64_t remaining = pin->sample_period_us - elapsed;
        TickType_t ticks = pdMS_TO_TICKS((remaining + 999) / 1000);

        if (ticks > 0)
          vTaskDelay(ticks);
      }
    }

    p[i] = gpio_get_level(fd) ? 1 : 0;
    pin->last_sample = esp_timer_get_time();
  }

  return size;
}
//=====================================================================
ssize_t driver_bus_gpio_write(eos_dev_t *dev, int fd, const void *data,
                              size_t size) {
  if (!IS_VALID_FD(fd)) {
    errno = EINVAL;
    return -1;
  }

  // Maybe ban opening?
  if (!GPIO_IS_VALID_OUTPUT_GPIO(fd)) {
    errno = EFAULT;
    return -1;
  }

  if (!data || size == 0) {
    errno = EINVAL;
    return -1;
  }

  gpio_pin_t *pin = &((gpio_bus_state_t *)dev->state)->pins[fd];
  const uint8_t *p = data;

  if (pin->cfg.mode != GPIO_MODE_OUTPUT) {
    pin->cfg.mode = GPIO_MODE_OUTPUT;

    if (gpio_config(&pin->cfg) != ESP_OK) {
      errno = EIO;
      return -1;
    }
  }

  for (size_t i = 0; i < size; i++) {
    uint8_t value;
    // TODO: skip spaces
    if (p[i] == 0 || p[i] == '0')
      value = 0;
    else if (p[i] == 1 || p[i] == '1')
      value = 1;
    else {
      errno = EINVAL;
      return -1;
    }

    if (pin->sample_period_us > 0 && pin->last_sample > 0) {
      int64_t elapsed = esp_timer_get_time() - pin->last_sample;

      if (elapsed < pin->sample_period_us) {
        int64_t remaining = pin->sample_period_us - elapsed;
        TickType_t ticks = pdMS_TO_TICKS((remaining + 999) / 1000);

        if (ticks > 0)
          vTaskDelay(ticks);
      }
    }

    if (gpio_set_level(fd, value) != ESP_OK) {
      errno = EIO;
      return -1;
    }

    pin->last_sample = esp_timer_get_time();
  }

  return size;
}
//=====================================================================
int driver_bus_gpio_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  if (!IS_VALID_FD(fd)) {
    errno = EINVAL;
    return -1;
  }

  gpio_pin_t *pin = &((gpio_bus_state_t *)dev->state)->pins[fd];

  switch (cmd) {

  case EOS_GPIO_IOCTL_SET_FLOATING:
    pin->cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    pin->cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    break;

  case EOS_GPIO_IOCTL_SET_PULLUP:
    pin->cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    pin->cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    break;

  case EOS_GPIO_IOCTL_SET_PULLDOWN:
    pin->cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    pin->cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    break;

  case EOS_GPIO_IOCTL_SET_PULLUPDOWN:
    pin->cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    pin->cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    break;

  case EOS_GPIO_IOCTL_SET_SAMPLE_PERIOD_US: {
    int32_t period = va_arg(args, int32_t);

    if (period < 0) {
      errno = EINVAL;
      return -1;
    }

    pin->sample_period_us = period;
    return 0;
  }

  default:
    errno = ENOTTY;
    return -1;
  }

  if (gpio_config(&pin->cfg) != ESP_OK) {
    errno = EIO;
    return -1;
  }

  return 0;
}
//=====================================================================
DIR *driver_bus_gpio_opendir(eos_dev_t *dev, const char *name) {
  EOS_LOGI("Entering gpio opendir with path=%s\n", name);

  gpio_dir_t *dirp = malloc(sizeof(gpio_dir_t));

  if (!dirp) {
    errno = ENOMEM;
    return NULL;
  }

  memset(dirp, 0, sizeof(gpio_dir_t));
  dirp->idx = 0;

  return (DIR *)dirp;
}
//=====================================================================
struct dirent *driver_bus_gpio_readdir(eos_dev_t *dev, DIR *pdir) {
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;

  if (!gpdir) {
    errno = EBADF;
    return NULL;
  }

  static struct dirent entry;

  for (;;) {
    if (!IS_VALID_FD(gpdir->idx))
      return NULL;

    if (GPIO_IS_VALID_GPIO(gpdir->idx)) {
      memset(&entry, 0, sizeof(entry));
      entry.d_type = DT_CHR;
      entry.d_ino = (ino_t)gpdir->idx;
      snprintf(entry.d_name, sizeof(entry.d_name), "%d", gpdir->idx);

      gpdir->idx++; // advance AFTER selecting entry
      return &entry;
    }

    gpdir->idx++;
  }
}
//=====================================================================
void driver_bus_gpio_seekdir(eos_dev_t *dev, DIR *pdir, long offset) {
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;

  if (!gpdir) {
    errno = EBADF;
    return;
  }

  gpdir->idx = offset;
}
//=====================================================================
long driver_bus_gpio_telldir(eos_dev_t *dev, DIR *pdir) {
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;

  if (!gpdir) {
    errno = EBADF;
    return -1;
  }

  return gpdir->idx;
}
//=====================================================================
int driver_bus_gpio_closedir(eos_dev_t *dev, DIR *pdir) {
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;

  if (!gpdir) {
    errno = EBADF;
    return -1;
  }

  free(pdir);

  return 0;
}
//=====================================================================
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// DRIVER MANIFEST:
///////////////////////////////////////////////////////////////////////
EOS_DRV_ATTR eos_drv_t driver_bus_gpio = {
    EOS_DRV_INIT,
    .flags = EOS_DRV_FLAG_NO_INDEX,
    .scope = "bus",
    .name = "gpio",
    .devname = "gpio",
    .init = driver_bus_gpio_init,
    .shutdown = driver_bus_gpio_shutdown,
    .open = driver_bus_gpio_open,
    .close = driver_bus_gpio_close,
    .read = driver_bus_gpio_read,
    .write = driver_bus_gpio_write,
    .ioctl = driver_bus_gpio_ioctl,
    .opendir = driver_bus_gpio_opendir,
    .readdir = driver_bus_gpio_readdir,
    .seekdir = driver_bus_gpio_seekdir,
    .telldir = driver_bus_gpio_telldir,
    .closedir = driver_bus_gpio_closedir,
};
///////////////////////////////////////////////////////////////////////

#endif
