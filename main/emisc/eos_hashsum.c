#include "emisc/eos_hashsum.h"
#include "emisc/psram_alloc.h"
#include "emisc/strlimits.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STREAM_CHUNK      4096u
#define YIELD_EVERY_BYTES (64u * 1024u)
#define LIST_LINE_MAX     (EOS_XLARGE_STR_LEN + 256u)

///////////////////////////////////////////////////////////////////////
// Streaming file reader
///////////////////////////////////////////////////////////////////////
int eos_stream_path(const char *path, eos_chunk_fn fn, void *arg, uint64_t *total_out) {
  const bool use_stdin = (path == NULL || strcmp(path, "-") == 0);
  int fd = STDIN_FILENO;
  if (!use_stdin) {
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
  }

  uint8_t *buf = (uint8_t *)eos_psram_malloc_prefer(STREAM_CHUNK);
  if (!buf) {
    if (!use_stdin) close(fd);
    errno = ENOMEM;
    return -1;
  }

  uint64_t total = 0;
  size_t since_yield = 0;
  int rc = 0;
  for (;;) {
    ssize_t n = read(fd, buf, STREAM_CHUNK);
    if (n < 0) {
      if (errno == EINTR) continue;
      rc = -1;
      break;
    }
    if (n == 0) break;
    total += (uint64_t)n;
    if (fn(arg, buf, (size_t)n) != 0) {
      rc = 1;
      break;
    }
    since_yield += (size_t)n;
    if (since_yield >= YIELD_EVERY_BYTES) {
      since_yield = 0;
      usleep(1000);
    }
  }

  int saved = errno;
  free(buf);
  if (!use_stdin) close(fd);
  errno = saved;
  if (total_out) *total_out = total;
  return rc;
}

///////////////////////////////////////////////////////////////////////
// Hashing a path
///////////////////////////////////////////////////////////////////////
static int hash_chunk_cb(void *arg, const uint8_t *data, size_t len) {
  eos_hash_update((eos_hash_ctx_t *)arg, data, len);
  return 0;
}

// Returns 0 on success, -1 on I/O error (errno set).
static int hash_path(const char *path, eos_hash_alg_t alg, uint8_t *digest) {
  eos_hash_ctx_t ctx;
  if (eos_hash_init(&ctx, alg) != 0) {
    errno = EINVAL;
    return -1;
  }
  if (eos_stream_path(path, hash_chunk_cb, &ctx, NULL) != 0) {
    int saved = errno;
    memset(&ctx, 0, sizeof(ctx));
    errno = saved;
    return -1;
  }
  eos_hash_final(&ctx, digest);
  return 0;
}

///////////////////////////////////////////////////////////////////////
// Line reader for checksum lists
///////////////////////////////////////////////////////////////////////
typedef struct {
  int fd;
  uint8_t *rbuf;
  size_t rpos;
  size_t rlen;
} line_reader_t;

// Returns the next byte, -1 at EOF, -2 on read error.
static int lr_getc(line_reader_t *r) {
  if (r->rpos == r->rlen) {
    ssize_t n;
    do {
      n = read(r->fd, r->rbuf, 1024);
    } while (n < 0 && errno == EINTR);
    if (n < 0) return -2;
    if (n == 0) return -1;
    r->rpos = 0;
    r->rlen = (size_t)n;
  }
  return r->rbuf[r->rpos++];
}

// Reads one line into line (NUL terminated, newline removed).
// Returns 1 for a line, 0 at EOF, -1 on read error. *too_long is set if the
// line did not fit; the remainder is discarded.
static int lr_getline(line_reader_t *r, char *line, size_t cap, bool *too_long) {
  size_t len = 0;
  bool any = false;
  *too_long = false;
  for (;;) {
    int c = lr_getc(r);
    if (c == -2) return -1;
    if (c == -1) {
      if (!any) return 0;
      break;
    }
    any = true;
    if (c == '\n') break;
    if (len + 1 < cap) line[len++] = (char)c;
    else *too_long = true;
  }
  line[len] = '\0';
  return 1;
}

///////////////////////////////////////////////////////////////////////
// Options
///////////////////////////////////////////////////////////////////////
typedef struct {
  bool check;
  bool binary;
  bool warn;
  bool quiet;
  bool status;
} sum_opts_t;

typedef struct {
  int properly_formatted;
  int improper;
  int mismatched;
  int unreadable;
} check_counts_t;

static void print_usage(const char *prog, eos_hash_alg_t alg) {
  printf("Usage: %s [OPTION]... [FILE]...\n"
         "Print or check %s checksums. With no FILE, or when FILE is -,\n"
         "read standard input.\n"
         "  -b, --binary   read in binary mode (prints '*' before the name)\n"
         "  -t, --text     read in text mode (default)\n"
         "  -c, --check    read checksums from FILEs and check them\n"
         "  -w, --warn     warn about improperly formatted lines (with --check)\n"
         "      --quiet    do not print OK for each verified file (with --check)\n"
         "      --status   print nothing, report only through the exit code\n"
         "  -h, --help     show this help\n",
         prog, eos_hash_name(alg));
}

///////////////////////////////////////////////////////////////////////
// Check mode
///////////////////////////////////////////////////////////////////////
static void check_one_list(const char *prog, const char *list_path, eos_hash_alg_t alg,
                           const sum_opts_t *opt, check_counts_t *counts) {
  const size_t dsz = eos_hash_digest_size(alg);
  const size_t hex_len = dsz * 2;

  const bool use_stdin = (strcmp(list_path, "-") == 0);
  int fd = STDIN_FILENO;
  if (!use_stdin) {
    fd = open(list_path, O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "%s: %s: %s\n", prog, list_path, strerror(errno));
      counts->unreadable++;
      return;
    }
  }

  char *line = (char *)eos_psram_malloc_prefer(LIST_LINE_MAX);
  uint8_t *rbuf = (uint8_t *)eos_psram_malloc_prefer(1024);
  if (!line || !rbuf) {
    fprintf(stderr, "%s: out of memory\n", prog);
    free(line);
    free(rbuf);
    if (!use_stdin) close(fd);
    counts->unreadable++;
    return;
  }

  line_reader_t lr = { .fd = fd, .rbuf = rbuf, .rpos = 0, .rlen = 0 };
  int lineno = 0;
  for (;;) {
    bool too_long = false;
    int got = lr_getline(&lr, line, LIST_LINE_MAX, &too_long);
    if (got < 0) {
      fprintf(stderr, "%s: %s: read error\n", prog, list_path);
      counts->unreadable++;
      break;
    }
    if (got == 0) break;
    lineno++;

    size_t len = strlen(line);
    if (len && line[len - 1] == '\r') line[--len] = '\0';
    if (len == 0 || line[0] == '#') continue;

    // "<hex>" + ' ' + (' ' | '*') + name
    uint8_t expected[EOS_HASH_MAX_DIGEST];
    bool ok = !too_long && len > hex_len + 2 &&
              line[hex_len] == ' ' && (line[hex_len + 1] == ' ' || line[hex_len + 1] == '*') &&
              eos_hash_from_hex(line, hex_len, expected, sizeof(expected)) == (int)dsz;
    if (!ok) {
      counts->improper++;
      if (opt->warn && !opt->status)
        fprintf(stderr, "%s: %s: %d: improperly formatted checksum line\n", prog, list_path, lineno);
      continue;
    }
    counts->properly_formatted++;

    const char *name = line + hex_len + 2;
    uint8_t actual[EOS_HASH_MAX_DIGEST];
    if (hash_path(name, alg, actual) != 0) {
      counts->unreadable++;
      if (!opt->status) {
        fprintf(stderr, "%s: %s: %s\n", prog, name, strerror(errno));
        printf("%s: FAILED open or read\n", name);
      }
      continue;
    }
    if (eos_hash_equal(expected, actual, dsz)) {
      if (!opt->status && !opt->quiet) printf("%s: OK\n", name);
    } else {
      counts->mismatched++;
      if (!opt->status) printf("%s: FAILED\n", name);
    }
  }

  free(line);
  free(rbuf);
  if (!use_stdin) close(fd);
}

///////////////////////////////////////////////////////////////////////
// Entry point
///////////////////////////////////////////////////////////////////////
int eos_hashsum_main(int argc, char **argv, eos_hash_alg_t alg, const char *prog) {
  sum_opts_t opt = { false, false, false, false, false };
  int first_file = argc;
  bool opts_done = false;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (opts_done || a[0] != '-' || a[1] == '\0') {
      first_file = i;
      break;
    }
    if (strcmp(a, "--") == 0) {
      opts_done = true;
      first_file = i + 1;
      continue;
    }
    if (a[1] == '-') {
      if (strcmp(a, "--check") == 0) opt.check = true;
      else if (strcmp(a, "--binary") == 0) opt.binary = true;
      else if (strcmp(a, "--text") == 0) opt.binary = false;
      else if (strcmp(a, "--warn") == 0) opt.warn = true;
      else if (strcmp(a, "--quiet") == 0) opt.quiet = true;
      else if (strcmp(a, "--status") == 0) opt.status = true;
      else if (strcmp(a, "--help") == 0) { print_usage(prog, alg); fflush(stdout); return 0; }
      else {
        fprintf(stderr, "%s: unrecognized option '%s'\nTry '%s --help'.\n", prog, a, prog);
        return 1;
      }
    } else {
      for (const char *c = a + 1; *c; c++) {
        switch (*c) {
          case 'c': opt.check = true; break;
          case 'b': opt.binary = true; break;
          case 't': opt.binary = false; break;
          case 'w': opt.warn = true; break;
          case 'h': print_usage(prog, alg); fflush(stdout); return 0;
          default:
            fprintf(stderr, "%s: invalid option -- '%c'\nTry '%s --help'.\n", prog, *c, prog);
            return 1;
        }
      }
    }
    first_file = i + 1;
  }

  int status = 0;

  if (opt.check) {
    check_counts_t counts = { 0, 0, 0, 0 };
    if (first_file >= argc) {
      check_one_list(prog, "-", alg, &opt, &counts);
    } else {
      for (int i = first_file; i < argc; i++) check_one_list(prog, argv[i], alg, &opt, &counts);
    }
    fflush(stdout);

    if (counts.properly_formatted == 0 && counts.unreadable == 0 && !opt.status)
      fprintf(stderr, "%s: no properly formatted %s checksum lines found\n", prog, eos_hash_name(alg));
    if (!opt.status) {
      if (counts.improper)
        fprintf(stderr, "%s: WARNING: %d line%s improperly formatted\n", prog, counts.improper,
                counts.improper == 1 ? " is" : "s are");
      if (counts.unreadable)
        fprintf(stderr, "%s: WARNING: %d listed file%s could not be read\n", prog, counts.unreadable,
                counts.unreadable == 1 ? "" : "s");
      if (counts.mismatched)
        fprintf(stderr, "%s: WARNING: %d computed checksum%s did NOT match\n", prog, counts.mismatched,
                counts.mismatched == 1 ? "" : "s");
    }
    if (counts.properly_formatted == 0 || counts.mismatched || counts.unreadable) status = 1;
    return status;
  }

  const int nfiles = (first_file >= argc) ? 1 : (argc - first_file);
  for (int n = 0; n < nfiles; n++) {
    const char *path = (first_file >= argc) ? "-" : argv[first_file + n];
    uint8_t digest[EOS_HASH_MAX_DIGEST];
    if (hash_path(path, alg, digest) != 0) {
      fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
      status = 1;
      continue;
    }
    char hex[EOS_HASH_MAX_HEX];
    eos_hash_to_hex(digest, eos_hash_digest_size(alg), hex);
    printf("%s %c%s\n", hex, opt.binary ? '*' : ' ', path);
  }
  fflush(stdout);
  return status;
}
