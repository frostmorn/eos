#include "sys/app.h"
#include "misc/fancymacro.h"
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#define USH_LINE_MAX 256

static int ush_readline(char *buf, size_t size) {
  size_t len = 0;

  for (;;) {
    char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);

    if (n == 1) {
      switch (ch) {
      case '\r':
      case '\n':
        putchar('\n');
        buf[len] = '\0';
        return (int)len;

      case '\b':
      case 0x7f: // DEL
        if (len) {
          len--;
          printf("\b \b");
          fflush(stdout);
        }
        break;

      default:
        if (len < size - 1) {
          buf[len++] = ch;
          putchar(ch);
          fflush(stdout);
        }
        break;
      }

      continue;
    }

    if (n == 0) {
      // EOF
      return -1;
    }

    if (errno == EAGAIN) {
      clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    perror("read");
    return -1;
  }
}

int ush_main(int argc, char **argv) {
  // this is surely a wrong place for that, so, let's assume we would change it
  // later
  // TODO: USB Composite device exposal
  usb_serial_jtag_driver_config_t cfg = {
      .tx_buffer_size = USH_LINE_MAX,
      .rx_buffer_size = USH_LINE_MAX,
  };

  // ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));

  // usb_serial_jtag_vfs_use_driver();

  // usb_serial_jtag_vfs_use_nonblocking();

  printf("\neos shell — type 'help' for available commands\n\n");

  char line[USH_LINE_MAX];

  for (;;) {
    printf("eos> ");
    fflush(stdout);

    if (ush_readline(line, sizeof(line)) < 0)
      break;

    if (!line[0])
      continue;

    if (!strcmp(line, "exit"))
      break;

    if (!strcmp(line, "help")) {
      eos_system("tree /bin");
      continue;
    }

    int ret = eos_system(line);

    if (ret)
      printf("exit code: %d\n", ret);
  }
  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t ush = {
    EOS_NATIVE_APP_INIT,     .filename = "ush",
    .name = "MicroShell",    .description = "EOS command line shell",
    .entry_point = ush_main,
};