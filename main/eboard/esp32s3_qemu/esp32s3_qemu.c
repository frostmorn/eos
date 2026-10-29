#include "pregen.h"
#ifdef EOS_TARGET_ESP32S3_QEMU
#include "eboard/board.h"
#include "ecore/capsmgr.h"
#include <esp_flash.h>

void eos_board_init() {
  // ================== RESERVED PINS ==================
  eos_cap_claim(EOS_CAPS_GPIO, 0, NULL);
  eos_cap_claim(EOS_CAPS_GPIO, 3, NULL);
  eos_cap_claim(EOS_CAPS_GPIO, 45, NULL);
  eos_cap_claim(EOS_CAPS_GPIO, 46, NULL);

  // ====================== FLASH ======================
  eos_dev_t *flash = eos_dev_alloc();
  flash->driver = eos_drv_find("storage", "flash");
  flash->cfg = (eos_cfg_t[]){{"chip", EOS_CFG_PTR, .val.ptr = esp_flash_default_chip},
                           {NULL}};
  eos_dev_attach(flash, eos_devtree_root());

  // ================== GPIO bus =======================
  eos_dev_t *gpio = eos_dev_alloc();
  gpio->driver = eos_drv_find("bus", "gpio");
  eos_dev_attach(gpio, eos_devtree_root());

#ifdef EOS_DRV_DISPLAY_QEMU_RGB_ENABLED
  // ===================== QEMU RGB Display =====================
  eos_dev_t *display = eos_dev_alloc();
  display->driver = eos_drv_find("display", "qemu_rgb");
  display->cfg = (eos_cfg_t[]){{"width", EOS_CFG_INT, .val.i = EOS_QEMU_DISPLAY_WIDTH},
                               {"height", EOS_CFG_INT, .val.i = EOS_QEMU_DISPLAY_HEIGHT},
                               {"bpp", EOS_CFG_INT, .val.i = 16},
                               {NULL}};
  eos_dev_attach(display, eos_devtree_root());
#endif
}
#endif
