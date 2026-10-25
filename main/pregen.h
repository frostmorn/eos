#pragma once

// Router header for board-specific pregen configurations.
// Each supported board provides its own board_pregen.h in main/eboard/<board>/.
// Selection is determined via Kconfig (CONFIG_EOS_BOARD_*) or target chip.

#include <sdkconfig.h>

#if defined(CONFIG_EOS_BOARD_JC4880P443C_I_W_Y) || defined(EOS_TARGET_JC4880P443C_I_W_Y)
#include "eboard/jc4880p443c_i_w_y/board_pregen.h"
#elif defined(CONFIG_EOS_BOARD_ESP32S3_QEMU) || defined(EOS_TARGET_ESP32S3_QEMU)
#include "eboard/esp32s3_qemu/board_pregen.h"
#elif defined(CONFIG_EOS_BOARD_LILKA_V23) || defined(EOS_TARGET_LILKAV2)
#include "eboard/lilka_v23/board_pregen.h"
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
#include "eboard/jc4880p443c_i_w_y/board_pregen.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "eboard/lilka_v23/board_pregen.h"
#else
#error "Unsupported target or board configuration for EOS"
#endif
