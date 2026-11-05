#include "ecore/bin.h"
#include "emisc/eos_hashsum.h"

int sha1sum_main(int argc, char **argv) {
  return eos_hashsum_main(argc, argv, EOS_HASH_SHA1, "sha1sum");
}

EOS_BIN_ATTR eos_bin_t sha1sum_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "sha1sum",
    .name = "SHA-1 Checksum",
    .group = "Utils",
    .description = "Compute or check SHA-1 message digests (legacy, not collision resistant)",
    .entry_point = sha1sum_main,
};
