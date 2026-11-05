#include "ecore/bin.h"
#include "emisc/eos_hash.h"
#include "emisc/eos_hashsum.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t crc;
} cksum_state_t;

static int cksum_chunk_cb(void *arg, const uint8_t *data, size_t len) {
  cksum_state_t *st = (cksum_state_t *)arg;
  st->crc = eos_cksum_update(st->crc, data, len);
  return 0;
}

static void print_usage(void) {
  printf("Usage: cksum [FILE]...\n"
         "Print the POSIX CRC checksum and byte count of each FILE.\n"
         "With no FILE, or when FILE is -, read standard input.\n");
}

int cksum_main(int argc, char **argv) {
  int first = 1;
  if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    print_usage();
    fflush(stdout);
    return 0;
  }
  if (argc > 1 && strcmp(argv[1], "--") == 0) first = 2;

  int status = 0;
  const int nfiles = (first >= argc) ? 1 : (argc - first);
  for (int n = 0; n < nfiles; n++) {
    const bool from_stdin = (first >= argc);
    const char *path = from_stdin ? "-" : argv[first + n];
    cksum_state_t st = { 0 };
    uint64_t total = 0;
    if (eos_stream_path(path, cksum_chunk_cb, &st, &total) != 0) {
      fprintf(stderr, "cksum: %s: %s\n", path, strerror(errno));
      status = 1;
      continue;
    }
    uint32_t crc = eos_cksum_final(st.crc, total);
    if (from_stdin) printf("%lu %lu\n", (unsigned long)crc, (unsigned long)total);
    else printf("%lu %lu %s\n", (unsigned long)crc, (unsigned long)total, path);
  }
  fflush(stdout);
  return status;
}

EOS_BIN_ATTR eos_bin_t cksum_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "cksum",
    .name = "CRC Checksum",
    .group = "Utils",
    .description = "Print the POSIX CRC checksum and byte count of files or standard input",
    .entry_point = cksum_main,
};
