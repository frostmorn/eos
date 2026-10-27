#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <esp_heap_caps.h>

#include "ecore/bin.h"
#include "ecore/ioctl.h"

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

static void test_doom_fire(int fd) {
  int w = 0, h = 0;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &w) != 0 || w <= 0)
    return;
  if (ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &h) != 0 || h <= 0)
    return;

  size_t fire_bytes = (size_t)w * (size_t)h;
  size_t frame_bytes = fire_bytes * sizeof(uint16_t);

#ifdef MALLOC_CAP_SPIRAM
  uint8_t *fire = heap_caps_calloc(w * h, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint16_t *buf = heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  uint8_t *fire = NULL;
  uint16_t *buf = NULL;
#endif
  if (!fire)
    fire = calloc(w * h, 1);
  if (!buf)
    buf = malloc(frame_bytes);

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

    if (write(fd, buf, frame_bytes) < 0)
      break;

    usleep(30 * 1000);
  }

cleanup:
  free(fire);
  free(buf);
}

static int test_disp_doom_fire_main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "/dev/fb0";

  int fd = open(path, O_RDWR);
  if (fd < 0) {
    perror("test_disp_doom_fire: open display device");
    return -1;
  }

  test_doom_fire(fd);

  close(fd);

  return 0;
}

EOS_BIN_ATTR eos_bin_t test_disp_doom_fire = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_GUI,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_disp_doom_fire",
    .name = "Doom Fire",
    .group = "Tests",
    .description = "Classic Doom procedural fire animation simulating framebuffer particle updates",
    .entry_point = test_disp_doom_fire_main};
