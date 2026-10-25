#include "ecore/bin.h"
#include "ecore/ioctl.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

static uint8_t pattern[] = {1, 0, 1, 1, 0, 1, 1, 1, 0, 1 ,1 , 1, 1, 0};

typedef struct{
  char filename[PATH_MAX];
  int fd;
  int pin_num;
} pin_file_t;

int soldercheck_main(int argc, char **argv){
  (void)argc;
  (void)argv;

  DIR *dir = opendir("/dev/gpio");
  if (!dir) {
    printf("Failed to open /dev/gpio\n");
    return -1;
  }

  // Get pin count; 
  size_t count_pins = 0;
  struct dirent *ent;
  while((ent = readdir(dir)) != NULL){
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    count_pins++;
  }

  if (count_pins == 0) {
    closedir(dir);
    printf("No GPIO pins found\n");
    return 0;
  }

  // allocate memory for open files
  pin_file_t *file_list = malloc(sizeof(pin_file_t) * count_pins);
  if (!file_list) {
    closedir(dir);
    printf("Out of memory\n");
    return -1;
  }

  // Zero allocated memory
  memset(file_list, 0, sizeof(pin_file_t) * count_pins);
  for (size_t i = 0; i < count_pins; i++) {
    file_list[i].fd = -1;
    file_list[i].pin_num = -1;
  }

  // Iterate over gpios
  rewinddir(dir);
  size_t index = 0;
  while((ent = readdir(dir)) != NULL){  
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    if (index >= count_pins)
      break;

    snprintf(file_list[index].filename, sizeof(file_list[index].filename), "/dev/gpio/%s", ent->d_name); 
    file_list[index].pin_num = atoi(ent->d_name);
    file_list[index].fd = open(file_list[index].filename, O_RDWR);
    if(file_list[index].fd >= 0){
      int fd = file_list[index].fd;
      ioctl(fd, EOS_GPIO_IOCTL_SET_PULLUP); 
      ioctl(fd, EOS_GPIO_IOCTL_SET_SAMPLE_PERIOD_US, 20); 
      ioctl(fd, EOS_GPIO_IOCTL_SET_IOMODE_BIN);
    }
    index++;          
  }
  // close dir;
  closedir(dir);

  size_t active_count = index;

  // Test pattern:
  for(size_t i = 0; i < active_count; i++){                 // PINA // file_list[i]
    if (file_list[i].fd < 0)
      continue;

    for(size_t j = 0; j < active_count; j++){           // PINB // file_list[j]
      if (i == j || file_list[j].fd < 0)
        continue;

      // Setup default pin state
      bool shorted = true;
      uint8_t pin_state = 0;
      bool test_failed = false;

      for(size_t k = 0; k < sizeof(pattern)/sizeof(pattern[0]); k++){  // pattern[k]
        ssize_t w_ret = write(file_list[i].fd, &pattern[k], 1); // write PINA
        if (w_ret < 0) {
          // Writing failed (e.g. input-only pin)
          test_failed = true;
          break;
        }

        ssize_t r_ret = read(file_list[j].fd, &pin_state, 1);  // read PINB
        if (r_ret < 0) {
          test_failed = true;
          break;
        }

        usleep(10);
        if (pattern[k] != pin_state){
          shorted = false;
          break;
        }
      }

      if (!test_failed && shorted){ 
        printf("Found short between GPIOs %s and %s\n", 
          file_list[i].filename, 
          file_list[j].filename
        );
      }
    }
    usleep(1000); // Feed watchdog / yield between iterations
  }

  // Close open files
  for(size_t i = 0; i < active_count; i++){
    if (file_list[i].fd >= 0){
      close(file_list[i].fd);
      usleep(10);
    }
  }

  // Free memory for open files
  free(file_list);
  
  return 0;
}

///////////////////////////////////////////////////////////////////////
// BIN MANIFEST:
///////////////////////////////////////////////////////////////////////
EOS_BIN_ATTR eos_bin_t soldercheck_app = {
  EOS_BIN_INITIALIZER,
  .flags       = 0,
  .filename    = "soldercheck",
  .name        = "Solder Check",
  .group       = "Diag",
  .description = "Probe GPIO pins with test patterns to detect solder bridges and shorts",
  .entry_point = soldercheck_main
};
///////////////////////////////////////////////////////////////////////
