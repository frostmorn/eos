#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
//
// Shared building blocks for the checksum / encoding command-line tools
// (md5sum, sha1sum, sha224sum, sha256sum, sha384sum, sha512sum, cksum, base64).

#include <stddef.h>
#include <stdint.h>
#include "emisc/eos_hash.h"

// Called for every chunk read from a stream. Return 0 to continue reading,
// non-zero to stop early.
typedef int (*eos_chunk_fn)(void *arg, const uint8_t *data, size_t len);

// Reads a file ("-" or NULL means standard input) in fixed-size chunks and
// feeds them to fn. Yields the CPU periodically so large inputs do not starve
// lower-priority tasks.
//   returns  0  end of file reached
//           -1  open/read error (errno is set)
//            1  stopped early by the callback
// total_out (optional) receives the number of bytes delivered to fn.
int eos_stream_path(const char *path, eos_chunk_fn fn, void *arg, uint64_t *total_out);

// Entry point shared by the *sum tools. prog is used in messages.
// Supports: -c/--check, -b/--binary, -t/--text, -w/--warn, --quiet, --status, -h/--help.
// Check lists must use the GNU "<hex>  <name>" format. Returns 0 on success.
int eos_hashsum_main(int argc, char **argv, eos_hash_alg_t alg, const char *prog);
