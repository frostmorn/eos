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

#include <dirent.h>
#include <driver/gpio.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include "emisc/kvec.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

#define PIN_2_MASK (PIN) (1ULL << PIN)
#define IS_VALID_FD(FD) (!((FD < 0) || (FD >= SOC_PIN_COUNT)))


///////////////////////////////////////////////////////////////////////
// DRIVER DATA:
///////////////////////////////////////////////////////////////////////
typedef struct{
  TaskHandle_t     task_owner;       // xTaskGetCurrentTaskHandle [can be NULL]
  long             debounce;         // count of microseconds till next read
  long             lastSample;       // time mark when last sample has been made
  gpio_config_t    cfg;              // pin configuration derived from idf
} gpio_pin_t;
//=====================================================================
typedef struct{
  uint32_t         esp_idf_fs_index;
  int              idx;              // also gpioNum
}gpio_dir_t;
//=====================================================================
// GPIO count is always constant, meaning if we want to have a blocking 
// operation on a pin, we no need more fds than pin count iself, which is,
// useful
typedef struct{
  gpio_fd_t        pins               [SOC_GPIO_PIN_COUNT];
  // pin direction? in/out
}gpio_bus_state_t;
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// DRIVER IMPLEMENTATION:
///////////////////////////////////////////////////////////////////////
//=====================================================================
bool driver_bus_gpio_init(eos_dev_t *dev){
  // GPIO INIT
  dev->state = malloc(sizeof(gpio_bus_state_t));
  memset(dev->state, 0, sizeof(gpio_bus_sate_t));

  gpio_bus_state_t * sate = dev->state;

  // Setup defult pin modes for each gpio
  for (int i=0; i < SOC_GPIO_PIN_COUNT; i++){ 
    // TODO: eos_caps_check
    state.fds[i].cfg = {
     .pin_bit_mask = PIN2_MASK(i),                                        
     .mode         =                                                        
                     GPIO_MODE_OUTPUT,                                      
                  // GPIO_MODE_DISABLE,
                  // GPIO_MODE_INPUT,
                  // GPIO_MODE_OUTPUT,
                  // GPIO_MODE_OUTPUT_OD,
                  // GPIO_MODE_INPUT_OUTPUT_OD,
                  // GPIO_MODE_INPUT_OUTPUT,
     .pull_up_en   =
                     GPIO_PULLUP_DISABLE,
                  // GPIO_PULLUP_ENABLE,
     .pull_down_en = GPIO_PULLDOWN_DISABLE,
                  // GPIO_PULLDOWN_ENABLE,                                  
     .intr_type    = GPIO_INTR_DISABLE, 
                  // GPIO_INTR_POSEDGE,
                  // GPIO_INTR_NEGEDGE,
                  // GPIO_INTR_ANYEDGE,
                  // GPIO_INTR_LOW_LEVEL,
                  // GPIO_INTR_HIGH_LEVEL,
     // TODO: there's an option to control hysteresis in that struct
     // but it's somehow hardware dependent
    };
    // TODO:Setup gpio_config
  }

  return true;
}
//=====================================================================
void driver_bus_gpio_shutdown(eos_dev_t *dev){
  // GPIO Cleanup
  free(dev->state);
}
//=====================================================================
int *driver_bus_gpio_open(eos_dev_t *dev, const char *path, int flags, int mode){
  EOS_LOGI("Opening gpio %s", path);
  // Duplicate
  char *gPath = strdup(path);

  // Find last Path token
  char *lastToken = NULL;
  while(char *token = strtok(gPath, "/")){
    lastToken = token;
  }

  // Retrieve gpio number
  int gpioNum = atoi(lastToken);

  // Ensure zero if zero
  if (gpioNum == 0 && (strcmp(lastToken, "0") !=0)){
    return -1;
  }
 
  // Ensure GPIO range 
  if (gpioNum < 0 || gpioNum >= SOC_PIN_COUNT){
    errno = EINVAL;
    return -1;
  }

  // Claiming pin
  if (!eos_cap_claim(EOS_CAPS_GPIO, gpioNum, dev)){
    errno = EBUSY;
    return -1;
  }

  // Trying to open GPIO
  gpio_bus_state_t * state = dev->state;
  if(state->pins[gpioNum].task_owner == NULL)
    state->pins[gpioNum].task_owner = xTaskGetCurrentTaskHandle();
  else
    gpioNum = -1; // also fd no [Already Opened]

  // Cleanup
  free(gPath);

  return gpioNum;
}
//=====================================================================
int driver_bus_gpio_close(eos_dev_t *dev, int fd){
  // Ensure GPIO range 
  if(!IS_VALID_FD(fd)){
    errno = EINVAL;
    return -1;
  }

  // Release pin
  eos_cap_release(EOS_CAPS_GPIO, gpioNum, dev);

  // Free pin related data
  gpio_bus_state_t *state = dev->state;
  state->pins[gpioNum].task_owner = NULL;
}
//=====================================================================
// @size used as count of samples to read
ssize_t eos_drv_read(eos_dev_t *dev, int fd, void *dst, size_t size){
  if (!IS_VALID_FD(fd)){
    errno = EINVAL;
    return -1;
  }

  for (size_t i = 0; i < size; i++){
    // GPIO READ PIN NO fd
    if (dev->state->pins[fd] 
    // TODO: DEBOUNCE
  }
}
//=====================================================================
ssize_t eos_drv_write(eos_dev_t *dev, int fd, void *data, size_t size){
// SUPPORT "1"/"0"

}
//=====================================================================
int eos_drv_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args){
  if (!IS_VALID_FD(fd)){
    errno = EINVAL;
    return -1;
  }

  gpio_bus_state_t * state = dev->state;

  switch(cmd){
    case EOS_GPIO_SET_FLOATING:{};
    case EOS_GPIO_SET_PULLUP:{};
    case EOS_GPIO_SET_PULLDOWN:{};
    case EOS_GPIO_SET_PULLUPDOWN:{};
    case EOS_GPIO_SET_DEBOUNCE:{};

  };
}
//=====================================================================
DIR * driver_bus_gpio_opendir(eos_dev_t *dev, const char *name){
  EOS_LOGI("Entering gpio opendir with path=%s\n", name);

  gpio_dir_t *dirp = malloc(sizeof(gpio_dir_t));

  if (!dirp){
    errno = ENOMEM;
    return NULL;
  }

  memset(dirp, 0, sizeof(gpio_dir_t));
  dirp->idx = -1;

  return (DIR *)dirp;
}
//=====================================================================
struct dirent *driver_bus_gpio_readdir(eos_dev_t *dev, DIR *pdir){
  // Check args
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;
  if (!gpdir) {
    errno = EBADF;
    return NULL;
  }

  // Proceed to next entry
  gpdir->idx++; 

  // Entry index is outside of pin range
  if (gpdir->idx < 0 || gpdir->idx > SOC_GPIO_PIN_COUNT){
    return NULL;
  }

  // TODO: should it be thread local?
  static struct dirent entry; 

  // Filling new entry data
  entry.d_type = DT_CHR;
  entry.d_ino = (ino_t) gpdir->idx;

  sprintf(entry.d_name, "%d", gpdir->idx); 
  
  return &entry;
}
//=====================================================================
void driver_bus_gpio_seekdir(eos_dev_t *dev, DIR *pdir, long offset){
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;
  
  if (!gpdir) {
    errno = EBADF;
  }
  
  gpdir->idx = offset;
}
//=====================================================================
long driver_bus_gpio_telldir(eos_dev_t *dev, DIR *pdir){
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;
  
  if (!gpdir) {
    errno = EBADF;
    return -1;
  }

  return gpdir->idx;
}
//=====================================================================
int driver_bus_gpio_closedir(eos_dev_t *dev, DIR *pdir){
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
    .scope    = "bus",
    .name     = "gpio", 
    .devname  = "gpio",
    .init     = driver_bus_gpio_init,
    .shutdown = driver_bus_gpio_shutdown,
    .open     = driver_bus_gpio_open,
    .close    = driver_bus_gpio_close,
    .read     = driver_bus_gpio_read,
    .write    = driver_bus_gpio_write,
    .opendir  = driver_bus_gpio_opendir,
    .readdir  = driver_bus_gpio_readdir,
    .seekdir  = driver_bus_gpio_seekdir,
    .telldir  = driver_bus_gpio_telldir,
    .closedir = driver_bus_gpio_closedir,
};
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// TODO:
///////////////////////////////////////////////////////////////////////
// ioctl() PULL_UP/PULL_DOWN/PULL_UP_DOWN
// read()
// write() 
///////////////////////////////////////////////////////////////////////


#endif
