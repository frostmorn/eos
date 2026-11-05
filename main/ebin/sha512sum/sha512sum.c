#include "ecore/bin.h"
#include "emisc/eos_hashsum.h"

int sha512sum_main(int argc, char **argv) {
  return eos_hashsum_main(argc, argv, EOS_HASH_SHA512, "sha512sum");
}

EOS_BIN_ATTR eos_bin_t sha512sum_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "sha512sum",
    .name = "SHA-512 Checksum",
    .group = "Utils",
    .description = "Compute or check SHA-512 message digests",
    .entry_point = sha512sum_main,
};
