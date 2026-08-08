#include <stdio.h>

#include "ecore/app.h"
#include "ecore/ioctl.h"
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <sys/ioctl.h>

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

int test_disp_colors_main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "/dev/display0";

  FILE *f = fopen(path, "w");

  if (!f) {
    perror(strerror(errno));
    return -1;
  }

  test_colors(f);

  fclose(f);

  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t test_disp_colors = {
    EOS_NATIVE_APP_INIT, .filename = "test_disp_colors",
    .name = "test_disp_colors", .entry_point = test_disp_colors_main};
