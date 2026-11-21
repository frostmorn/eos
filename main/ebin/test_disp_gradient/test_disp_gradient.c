#include "ecore/bin.h"
#include "ecore/ioctl.h"
#include "ebin/test_common.h"
#include "esp_timer.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int test_disp_gradient_main(int argc, char **argv) {
  printf("Starting Display Test App...\n");

  eos_disp_test_args_t args = eos_disp_parse_args(argc, argv);
  uint32_t active_fmt = EOS_DISPLAY_FMT_RGB565;
  uint32_t orig_fmt = EOS_DISPLAY_FMT_RGB565;
  int fd = eos_disp_open_and_configure(&args, &active_fmt, &orig_fmt);
  if (fd < 0) {
    perror("Failed to open display device");
    return -1;
  }

  int width = 0, height = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &width) != 0 || width <= 0) {
    eos_disp_close_and_restore(fd, orig_fmt, active_fmt);
    return -1;
  }
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &height) != 0 || height <= 0) {
    eos_disp_close_and_restore(fd, orig_fmt, active_fmt);
    return -1;
  }

  printf("Display resolution: %dx%d (format=%lu)\n", width, height, (unsigned long)active_fmt);

  size_t bpp = eos_display_format_bytes_per_pixel(active_fmt);
  size_t buf_size = (size_t)width * height * bpp;
  uint8_t *frame_buffer = (uint8_t *)malloc(buf_size);
  if (!frame_buffer) {
    perror("Failed to allocate frame buffer");
    eos_disp_close_and_restore(fd, orig_fmt, active_fmt);
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

      uint8_t r = (uint8_t)(127.5f + 127.0f * sinf(phase_r));
      uint8_t g = (uint8_t)(127.5f + 127.0f * sinf(phase_g));
      uint8_t b = (uint8_t)(127.5f + 127.0f * sinf(phase_b));

      if (active_fmt == EOS_DISPLAY_FMT_RGB888) {
        for (int x = 0; x < width; x++) {
          size_t idx = ((size_t)y * width + x) * 3;
          frame_buffer[idx + 0] = r;
          frame_buffer[idx + 1] = g;
          frame_buffer[idx + 2] = b;
        }
      } else {
        uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        uint16_t *fb16 = (uint16_t *)frame_buffer;
        for (int x = 0; x < width; x++) {
          fb16[y * width + x] = color;
        }
      }
    }

    if (write(fd, frame_buffer, buf_size) < 0) {
      perror("Failed to write to display");
      break;
    }

    frame_count++;
    fps_frame_count++;
    int64_t now = esp_timer_get_time();
    if (now - last_fps_time >= EOS_US_PER_SEC) { // Every 1 second
      count_tests++;
      float fps = (float)fps_frame_count / ((float)(now - last_fps_time) / (float)EOS_US_PER_SEC);
      printf("FPS: %.2f\n", fps);
      fps_frame_count = 0;
      last_fps_time = now;
    }
    if (count_tests == DISP_GRADIENT_TIME)
      break;
  }

  free(frame_buffer);
  eos_disp_close_and_restore(fd, orig_fmt, active_fmt);
  return 0;
}

EOS_BIN_ATTR const eos_bin_t display_test_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI | EOS_BIN_FLAG_ROLE_FULL,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_gradient",
    .name = "Display Gradient",
    .group = "Tests",
    .description = "Animated RGB gradient demo measuring real-time framebuffer framerate (FPS) in RGB565 / RGB888",
    .entry_point = test_disp_gradient_main};
