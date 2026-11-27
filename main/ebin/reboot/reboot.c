#include "includes.h"
#include "reboot.h"
#include "ecore/bin.h"
#include "ecore/kcmd.h"
#include "ecore/proc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void reboot_print_usage(void) {
  printf("Usage: reboot [-b|--bootloader] [-k <cmd> | -- <args...>] [-h|--help]\n");
}

int reboot_main(int argc, char **argv) {
  bool bootloader = false;
  char kcmd_buf[EOS_KCMD_MAX_LEN] = {0};
  bool has_kcmd = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      reboot_print_usage();
      return 0;
    }
    if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bootloader") == 0) {
      bootloader = true;
      continue;
    }
    if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
      strlcpy(kcmd_buf, argv[++i], sizeof(kcmd_buf));
      has_kcmd = true;
      continue;
    }
    if (strcmp(argv[i], "--") == 0) {
      size_t pos = 0;
      for (int j = i + 1; j < argc && pos + 1 < sizeof(kcmd_buf); j++) {
        if (j > i + 1 && pos + 1 < sizeof(kcmd_buf)) {
          kcmd_buf[pos++] = ' ';
        }
        size_t alen = strlen(argv[j]);
        if (pos + alen >= sizeof(kcmd_buf)) {
          alen = sizeof(kcmd_buf) - pos - 1;
        }
        memcpy(kcmd_buf + pos, argv[j], alen);
        pos += alen;
      }
      kcmd_buf[pos] = '\0';
      has_kcmd = (pos > 0);
      break;
    }
    reboot_print_usage();
    return 1;
  }

  if (bootloader) {
    printf("Rebooting to bootloader...\n");
    fflush(stdout);
    usleep(EOS_POLL_INTERVAL_MS * (useconds_t)EOS_US_PER_MS);
    eos_reboot(true);
    return 0;
  }

  printf("Rebooting system%s%s...\n", has_kcmd ? " with kcmd: " : "", has_kcmd ? kcmd_buf : "");
  fflush(stdout);
  usleep(EOS_POLL_INTERVAL_MS * (useconds_t)EOS_US_PER_MS);
  if (has_kcmd) {
    eos_reboot_with_kcmd(kcmd_buf);
  } else {
    eos_reboot(false);
  }
  return 0;
}

EOS_BIN_ATTR eos_bin_t reboot_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "reboot",
    .name = "Reboot System",
    .group = "System",
    .description = "Restart the microcontroller",
    .entry_point = reboot_main};
