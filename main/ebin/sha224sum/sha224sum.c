#include "ecore/bin.h"
#include "emisc/eos_hashsum.h"

int sha224sum_main(int argc, char **argv) {
  return eos_hashsum_main(argc, argv, EOS_HASH_SHA224, "sha224sum");
}

EOS_BIN_ATTR eos_bin_t sha224sum_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "sha224sum",
    .name = "SHA-224 Checksum",
    .group = "Utils",
    .description = "Compute or check SHA-224 message digests",
    .entry_point = sha224sum_main,
};
