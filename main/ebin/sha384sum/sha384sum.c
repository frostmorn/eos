#include "ecore/bin.h"
#include "emisc/eos_hashsum.h"

int sha384sum_main(int argc, char **argv) {
  return eos_hashsum_main(argc, argv, EOS_HASH_SHA384, "sha384sum");
}

EOS_BIN_ATTR eos_bin_t sha384sum_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "sha384sum",
    .name = "SHA-384 Checksum",
    .group = "Utils",
    .description = "Compute or check SHA-384 message digests",
    .entry_point = sha384sum_main,
};
