#include <stdio.h>

#include "disp_test_colors.h"
#include "driver/display/display.h"
#include <sys/ioctl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static void test_colors(FILE *f) {

  int fd = fileno(f);
  int w = ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH);
  int h = ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT);

  uint16_t *buf = malloc(w * h * sizeof(uint16_t));
  if (!buf)
    return;

  // Red
  for (int i = 0; i < w * h; i++)
    buf[i] = 0xF800;

  fwrite(buf, w * h * sizeof(uint16_t), 1, f);
  vTaskDelay(pdMS_TO_TICKS(500));

  // Green
  for (int i = 0; i < w * h; i++)
    buf[i] = 0x07E0;
  fwrite(buf, w * h * sizeof(uint16_t), 1, f);
  vTaskDelay(pdMS_TO_TICKS(500));

  // Blue
  for (int i = 0; i < w * h; i++)
    buf[i] = 0x001F;
  fwrite(buf, w * h * sizeof(uint16_t), 1, f);
  vTaskDelay(pdMS_TO_TICKS(500));

  // Gradient
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      buf[y * w + x] = ((x * 31 / w) << 11) | ((y * 63 / h) << 5);
  fwrite(buf, w * h * sizeof(uint16_t), 1, f);

  free(buf);
}

int disp_test_colors_main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: disp_test_colors <path>\n");
    return 1;
  }

  const char *path = argv[1];

  FILE *f = fopen(path, "w");
  test_colors(f);

  fclose(f);

  return 0;
}