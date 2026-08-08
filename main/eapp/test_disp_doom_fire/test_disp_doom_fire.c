#include "ecore/app.h"
#include "ecore/ioctl.h"
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

static uint16_t fire_palette(uint8_t heat) {
  uint8_t r, g, b;

  if (heat < 85) {
    r = heat * 3;
    g = 0;
    b = 0;
  } else if (heat < 170) {
    r = 255;
    g = (heat - 85) * 3;
    b = 0;
  } else {
    r = 255;
    g = 255;
    b = (heat - 170) * 3;
  }

  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void test_doom_fire(FILE *f) {
  int fd = fileno(f);

  int w = ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH);
  int h = ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT);

  uint8_t *fire = calloc(w * h, 1);
  uint16_t *buf = malloc(w * h * sizeof(uint16_t));

  if (!fire || !buf)
    goto cleanup;

  // ignite bottom
  for (int x = 0; x < w; x++)
    fire[(h - 1) * w + x] = 255;

  while (1) {

    // propagate fire upward
    for (int y = 0; y < h - 1; y++) {
      for (int x = 0; x < w; x++) {

        int src = (y + 1) * w + x;

        int decay = rand() & 3;

        int dst_x = x;

        if (decay && dst_x > 0)
          dst_x--;

        int value = fire[src] - decay;

        if (value < 0)
          value = 0;

        fire[y * w + dst_x] = value;
      }
    }

    // keep bottom hot
    for (int x = 0; x < w; x++)
      fire[(h - 1) * w + x] = 255;

    // convert heat -> RGB565
    for (int i = 0; i < w * h; i++)
      buf[i] = fire_palette(fire[i]);

    fwrite(buf, w * h * sizeof(uint16_t), 1, f);

    vTaskDelay(pdMS_TO_TICKS(30));
  }

cleanup:
  free(fire);
  free(buf);
}

static int test_disp_doom_fire_main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "/dev/display0";

  FILE *f = fopen(path, "w");

  if (!f) {
    perror(strerror(errno));
    return -1;
  }

  test_doom_fire(f);

  fclose(f);

  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t test_disp_doom_fire = {
    EOS_NATIVE_APP_INIT, .filename = "test_disp_doom_fire",
    .name = "test_disp_doom_fire", .entry_point = test_disp_doom_fire_main};
