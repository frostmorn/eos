#include <stdio.h>

#include "disp_test_bars.h"
#include "driver/display/display.h"
#include <sys/ioctl.h>

static void draw_bars(FILE *f) {

  int fd = fileno(f);
  int w = ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH);
  int h = ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT);

  uint16_t *buf = malloc(w * h * sizeof(uint16_t));
  if (!buf)
    return;

  // RGB565 color bars
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint16_t c;

      if (x < w / 6)
        c = 0xF800; // red
      else if (x < w * 2 / 6)
        c = 0x07E0; // green
      else if (x < w * 3 / 6)
        c = 0x001F; // blue
      else if (x < w * 4 / 6)
        c = 0xFFE0; // yellow
      else if (x < w * 5 / 6)
        c = 0xF81F; // magenta
      else
        c = 0x07FF; // cyan

      // 1-pixel white border
      if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
        c = 0xFFFF;

      // White diagonals
      if (x == y || x == (w - 1 - y))
        c = 0xFFFF;

      buf[y * w + x] = c;
    }
  }

  // Write one full frame
  fwrite(buf, w * h * sizeof(uint16_t), 1, f);

  free(buf);
}

int disp_test_bars_main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: disp_test_bars <path>\n");
    return 1;
  }

  const char *path = argv[1];

  FILE *f = fopen(path, "w");
  draw_bars(f);

  fclose(f);

  return 0;
}