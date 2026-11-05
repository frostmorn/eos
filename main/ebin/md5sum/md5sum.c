#include "ecore/bin.h"
#include "emisc/eos_hashsum.h"

int md5sum_main(int argc, char **argv) {
  return eos_hashsum_main(argc, argv, EOS_HASH_MD5, "md5sum");
}

EOS_BIN_ATTR eos_bin_t md5sum_app = {
    EOS_BIN_INITIALIZER,
    .flags = EOS_BIN_FLAG_HIDDEN,
    .filename = "md5sum",
    .name = "MD5 Checksum",
    .group = "Utils",
    .description = "Compute or check MD5 message digests (legacy, not collision resistant)",
    .entry_point = md5sum_main,
};
