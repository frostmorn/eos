#include "ecore/bin.h"
#include "emisc/eos_base64.h"
#include "emisc/eos_hashsum.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  bool decode;
  eos_b64enc_t enc;
  eos_b64dec_t dec;
  int rc;
} b64_job_t;

static int b64_emit(void *arg, const void *data, size_t len) {
  (void)arg;
  return fwrite(data, 1, len, stdout) == len ? 0 : 1;
}

static int b64_chunk_cb(void *arg, const uint8_t *data, size_t len) {
  b64_job_t *job = (b64_job_t *)arg;
  job->rc = job->decode ? eos_b64dec_update(&job->dec, data, len)
                        : eos_b64enc_update(&job->enc, data, len);
  return job->rc != EOS_B64_OK;
}

static void print_usage(void) {
  printf("Usage: base64 [OPTION]... [FILE]\n"
         "Base64 encode or decode FILE, or standard input, to standard output.\n"
         "  -d, --decode           decode data\n"
         "  -i, --ignore-garbage   when decoding, ignore non-alphabet characters\n"
         "  -w, --wrap=COLS        wrap encoded lines after COLS characters (default 76,\n"
         "                         0 disables wrapping)\n"
         "  -h, --help             show this help\n");
}

static bool parse_cols(const char *s, size_t *out) {
  if (!s || !*s) return false;
  char *end = NULL;
  errno = 0;
  unsigned long v = strtoul(s, &end, 10);
  if (errno != 0 || *end != '\0') return false;
  *out = (size_t)v;
  return true;
}

int base64_main(int argc, char **argv) {
  bool decode = false, ignore_garbage = false;
  size_t wrap = 76;
  const char *file = "-";
  bool have_file = false;

  bool opts_done = false;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (opts_done || a[0] != '-' || a[1] == '\0') {
      if (have_file) { fprintf(stderr, "base64: extra operand '%s'\n", a); return 1; }
      file = a;
      have_file = true;
      continue;
    }
    if (strcmp(a, "--") == 0) { opts_done = true; continue; }
    if (a[1] == '-') {
      if (strcmp(a, "--decode") == 0) decode = true;
      else if (strcmp(a, "--ignore-garbage") == 0) ignore_garbage = true;
      else if (strcmp(a, "--help") == 0) { print_usage(); fflush(stdout); return 0; }
      else if (strcmp(a, "--wrap") == 0) {
        if (i + 1 >= argc || !parse_cols(argv[i + 1], &wrap)) {
          fprintf(stderr, "base64: invalid wrap size\n");
          return 1;
        }
        i++;
      } else if (strncmp(a, "--wrap=", 7) == 0) {
        if (!parse_cols(a + 7, &wrap)) { fprintf(stderr, "base64: invalid wrap size: '%s'\n", a + 7); return 1; }
      } else {
        fprintf(stderr, "base64: unrecognized option '%s'\nTry 'base64 --help'.\n", a);
        return 1;
      }
      continue;
    }
    // Short options may be clustered (-di); -w takes the rest of the cluster or the next argument.
    for (const char *c = a + 1; *c; c++) {
      if (*c == 'd') decode = true;
      else if (*c == 'i') ignore_garbage = true;
      else if (*c == 'h') { print_usage(); fflush(stdout); return 0; }
      else if (*c == 'w') {
        const char *val = c[1] ? c + 1 : (i + 1 < argc ? argv[++i] : NULL);
        if (!parse_cols(val, &wrap)) { fprintf(stderr, "base64: invalid wrap size\n"); return 1; }
        break;
      } else {
        fprintf(stderr, "base64: invalid option -- '%c'\nTry 'base64 --help'.\n", *c);
        return 1;
      }
    }
  }

  b64_job_t job = { .decode = decode, .rc = EOS_B64_OK };
  if (decode) eos_b64dec_init(&job.dec, ignore_garbage, b64_emit, NULL);
  else eos_b64enc_init(&job.enc, wrap, b64_emit, NULL);

  int sr = eos_stream_path(file, b64_chunk_cb, &job, NULL);
  if (sr < 0) {
    fprintf(stderr, "base64: %s: %s\n", file, strerror(errno));
    return 1;
  }
  if (job.rc == EOS_B64_OK) job.rc = decode ? eos_b64dec_final(&job.dec) : eos_b64enc_final(&job.enc);
  fflush(stdout);

  if (job.rc == EOS_B64_INVALID) {
    fprintf(stderr, "base64: invalid input\n");
    return 1;
  }
  if (job.rc != EOS_B64_OK) {
    fprintf(stderr, "base64: write error\n");
    return 1;
  }
  return 0;
}

EOS_BIN_ATTR eos_bin_t base64_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "base64",
    .name = "Base64",
    .group = "Utils",
    .description = "Base64 encode or decode files or standard input",
    .entry_point = base64_main,
};
