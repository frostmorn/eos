#include "ecore/app.h"
#include "edriver/display/display.h"
#include "esp_timer.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DISP_GRADIENT_TIME 3

static int test_disp_gradient_main(int argc, char **argv) {
  printf("Starting Display Test App...\n");

  const char *path = argc > 1 ? argv[1] : "/dev/display0";

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    perror("Failed to open display device");
    return -1;
  }

  int width = ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH);
  int height = ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT);

  printf("Display resolution: %dx%d\n", width, height);

  size_t buf_size = (size_t)width * height * 2;
  uint16_t *frame_buffer = malloc(buf_size);
  if (!frame_buffer) {
    perror("Failed to allocate frame buffer");
    close(fd);
    return -1;
  }

  int count_tests = 0;
  int frame_count = 0;
  int fps_frame_count = 0;
  int64_t last_fps_time = esp_timer_get_time();

  while (1) {
    for (int y = 0; y < height; y++) {
      // Calculate color based on Y and an offset for animation
      float phase_r =
          (float)y / height * 3.14159f * 2.0f + (float)frame_count / 20.0f;
      float phase_g = (float)y / height * 3.14159f * 2.0f +
                      (float)frame_count / 30.0f + 2.0f;
      float phase_b = (float)y / height * 3.14159f * 2.0f +
                      (float)frame_count / 40.0f + 4.0f;

      uint8_t r = (uint8_t)(128 + 127 * sinf(phase_r));
      uint8_t g = (uint8_t)(128 + 127 * sinf(phase_g));
      uint8_t b = (uint8_t)(128 + 127 * sinf(phase_b));
      uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

      for (int x = 0; x < width; x++) {
        frame_buffer[y * width + x] = color;
      }
    }

    if (write(fd, frame_buffer, buf_size) < 0) {
      perror("Failed to write to display");
      break;
    }

    frame_count++;
    fps_frame_count++;
    int64_t now = esp_timer_get_time();
    if (now - last_fps_time >= 1000000) { // Every 1 second
      count_tests++;
      float fps = (float)fps_frame_count / ((now - last_fps_time) / 1000000.0f);
      printf("FPS: %.2f\n", fps);
      fps_frame_count = 0;
      last_fps_time = now;
    }
    if (count_tests == DISP_GRADIENT_TIME)
      break;
  }

  free(frame_buffer);
  close(fd);
  return 0;
}

EOS_NATIVE_APP_ATTR const eos_native_app_manifest_t display_test_app = {
    .magic = EOS_NATIVE_APP_MAGIC,
    .filename = "test_disp_gradient",
    .name = "Display Test",
    .group = "Tools",
    .description = "Graphical demo with FPS logging",
    .entry_point = test_disp_gradient_main};
