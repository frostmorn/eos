#include "ecore/bin.h"
#include "emisc/eos_hashsum.h"

int sha256sum_main(int argc, char **argv) {
  return eos_hashsum_main(argc, argv, EOS_HASH_SHA256, "sha256sum");
}

EOS_BIN_ATTR eos_bin_t sha256sum_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "sha256sum",
    .name = "SHA-256 Checksum",
    .group = "Utils",
    .description = "Compute or check SHA-256 message digests",
    .entry_point = sha256sum_main,
};
