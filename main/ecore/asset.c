#include "asset.h"

extern const uint8_t init_sh_start[] asm("_binary_init_sh_start");
extern const uint8_t init_sh_end[] asm("_binary_init_sh_end");

const eos_asset_t *eos_get_init_asset(void) {
  static eos_asset_t asset = {
      .filename = "init.sh",
      .data = init_sh_start,
      .size = 0
  };
  if (asset.size == 0) {
    asset.size = (size_t)(init_sh_end - init_sh_start);
  }
  return &asset;
}
