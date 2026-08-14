#include <stdio.h>
#include "ecore/app.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>

// Simple utility designed to open file and write it's contents to stdout
#define CAT_BUFFER_SIZE 512 // Standard size of sdcard sector

int cat_main(int argc, char **argv){

  char buffer[CAT_BUFFER_SIZE];

  if (argc < 2){
    printf("cat: Usage cat <filename>\n");
    return -1;
  }

  const char* path = argv[1];  
 
  FILE *fp = fopen(path, "r");

  if (!fp){
    printf("Can't open file %s. %d: %s\n", path, errno, strerror(errno));
    return -1;
  }

  size_t count_read = 0;

  while ((count_read = fread(buffer, 1, CAT_BUFFER_SIZE, fp))){ 
    fwrite(buffer, 1, count_read, stdout);
   
    // TODO: solve WDT problem
    // usleep(10); 
    vTaskDelay((pdMS_TO_TICKS(10)));
  }

  fclose(fp);

  return 0;
}


EOS_NATIVE_APP_ATTR eos_native_app_manifest_t cat_app = {
    EOS_NATIVE_APP_INIT, .filename = "cat", .name = "cat",
    .entry_point = cat_main};
