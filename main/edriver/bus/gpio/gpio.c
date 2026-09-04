#include "includes.h"
#ifdef EOS_DRV_BUS_GPIO_ENABLED
#include <dirent.h>
#include <driver/gpio.h>
#include <errno.h>
#include "emisc/kvec.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"

// GPIO bus exposes a list of files /dev/gpio/X where X is a pin no with 
// ability to write 1 or 0

// /dev/gpio/

typedef struct{
  uint32_t esp_idf_fs_index;
  int idx;
}gpio_dir_t;

typedef struct{
//  kvec_t(gpio_dir_t) dirs;
}gpio_bus_state_t;

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

struct dirent *driver_bus_gpio_readdir(eos_dev_t *dev, DIR *pdir){
  // Check args
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;

  if (!gpdir) {
    errno = EBADF;
    return NULL;
  }
  EOS_LOGI("Entering gpio readdir idx =%d", gpdir->idx );

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

void driver_bus_gpio_seekdir(eos_dev_t *dev, DIR *pdir, long offset){

}

long driver_bus_gpio_telldir(eos_dev_t *dev, DIR *pdir){
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;
  
  if (!gpdir) {
    errno = EBADF;
    return -1;
  }

  return gpdir->idx;
}

int driver_bus_gpio_closedir(eos_dev_t *dev, DIR *pdir){
  gpio_dir_t *gpdir = (gpio_dir_t *)pdir;

  if (!gpdir) {
    errno = EBADF;
    return -1;
  }

  free(pdir);

  return 0;
}

EOS_DRV_ATTR eos_drv_t driver_bus_gpio = {
    EOS_DRV_INIT, 
    .scope = "bus",
    .name = "gpio", 
    .devname = "gpio",
    .opendir = driver_bus_gpio_opendir,
    .readdir = driver_bus_gpio_readdir,
    .seekdir = driver_bus_gpio_seekdir,
    .telldir = driver_bus_gpio_telldir,
    .closedir = driver_bus_gpio_closedir,
   
};

#endif
