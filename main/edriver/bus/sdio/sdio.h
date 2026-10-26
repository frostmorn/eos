#pragma once

#include "ecore/driver.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <soc/soc_caps.h>

#if SOC_SDMMC_HOST_SUPPORTED
#include <driver/sdmmc_host.h>
#include <driver/sdmmc_types.h>
#include <sdmmc_cmd.h>
#endif

#if defined(SOC_SDMMC_IO_POWER_EXTERNAL) && SOC_SDMMC_IO_POWER_EXTERNAL
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#endif

typedef struct {
  int slot;
  int width;
  uint32_t freq_khz;
  int clk_pin;
  int cmd_pin;
  int d0_pin;
  int d1_pin;
  int d2_pin;
  int d3_pin;
  int cd_pin;
  int wp_pin;
  int rst_pin;
  int ldo_chan;
  bool host_initialized;
  bool slot_initialized;
#if SOC_SDMMC_HOST_SUPPORTED
  sdmmc_host_t host_cfg;
  sdmmc_slot_config_t slot_cfg;
#if defined(SOC_SDMMC_IO_POWER_EXTERNAL) && SOC_SDMMC_IO_POWER_EXTERNAL
  sd_pwr_ctrl_handle_t pwr_ctrl_handle;
#endif
  sdmmc_card_t card;
#endif
  bool card_probed;
  SemaphoreHandle_t mutex;
  int attached_count;
  off_t offset;
} sdio_state_t;

#if SOC_SDMMC_HOST_SUPPORTED
esp_err_t eos_sdio_get_host(eos_dev_t *dev, sdmmc_host_t *out_host);
esp_err_t eos_sdio_get_card(eos_dev_t *dev, sdmmc_card_t **out_card);
esp_err_t eos_sdio_probe_card(eos_dev_t *dev, sdmmc_card_t *out_card);
#endif
esp_err_t eos_sdio_reset_slave(eos_dev_t *dev);
