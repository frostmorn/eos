#include "includes.h"
#ifdef EOS_DRIVER_STORAGE_SD_ENABLED

#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <sdmmc_cmd.h>

#include "sys/capsmgr.h"
#include "sys/driver.h"

// ── State ─────────────────────────────────────────────────────

typedef struct {
  sdmmc_card_t *card;
  sdspi_dev_handle_t handle;
  spi_host_device_t host;
} sd_state_t;

// ── Init / Shutdown ───────────────────────────────────────────

bool driver_storage_sd_init(eos_dev_t *dev) {
  sd_state_t *state = malloc(sizeof(sd_state_t));
  if (!state)
    return false;
  memset(state, 0, sizeof(sd_state_t));
  dev->state = state;

  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  if (cs_pin < 0 || !eos_cap_alloc(EOS_CAPS_GPIO, cs_pin, dev)) {
    free(state);
    return false;
  }

  state->host =
      (spi_host_device_t)eos_cfg_get_i(dev->parent->cfg, "host", SPI2_HOST);

  int32_t freq = eos_cfg_get_i(dev->cfg, "clock_speed_hz", SPI_MASTER_FREQ_20M);

  // Add SD card as SPI device
  sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev_cfg.host_id = state->host;
  dev_cfg.gpio_cs = cs_pin;

  if (sdspi_host_init_device(&dev_cfg, &state->handle) != ESP_OK) {
    eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);
    free(state);
    return false;
  }

  // Probe card
  sdmmc_host_t host_cfg = SDSPI_HOST_DEFAULT();
  host_cfg.slot = state->handle;
  host_cfg.max_freq_khz = freq / 1000;

  state->card = malloc(sizeof(sdmmc_card_t));
  if (!state->card) {
    sdspi_host_remove_device(state->handle);
    eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);
    free(state);
    return false;
  }

  if (sdmmc_card_init(&host_cfg, state->card) != ESP_OK) {
    free(state->card);
    sdspi_host_remove_device(state->handle);
    eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);
    free(state);
    return false;
  }

  // Print card info
  sdmmc_card_print_info(stdout, state->card);

  return true;
}

void driver_storage_sd_shutdown(eos_dev_t *dev) {
  sd_state_t *state = dev->state;
  if (!state)
    return;

  if (state->card)
    free(state->card);
  if (state->handle)
    sdspi_host_remove_device(state->handle);

  int32_t cs_pin = eos_pin_get_no(dev->pins, "cs");
  if (cs_pin >= 0)
    eos_cap_free(EOS_CAPS_GPIO, cs_pin, dev);

  free(state);
  dev->state = NULL;
}

// ── IO — raw block operations ─────────────────────────────────

int driver_storage_sd_read(eos_dev_t *dev, void *buf, size_t len) {
  sd_state_t *state = dev->state;
  if (!state || !state->card)
    return -1;

  size_t block_size = state->card->csd.sector_size;
  size_t num_blocks = len / block_size;
  if (num_blocks == 0)
    return -1;

  return sdmmc_read_sectors(state->card, buf, 0, num_blocks) == ESP_OK
             ? (int)len
             : -1;
}

int driver_storage_sd_write(eos_dev_t *dev, void *buf, size_t len) {
  sd_state_t *state = dev->state;
  if (!state || !state->card)
    return -1;

  size_t block_size = state->card->csd.sector_size;
  size_t num_blocks = len / block_size;
  if (num_blocks == 0)
    return -1;

  return sdmmc_write_sectors(state->card, buf, 0, num_blocks) == ESP_OK
             ? (int)len
             : -1;
}

EOS_DRIVER_ATTR eos_driver_t driver_storage_sd = {
    .scope = "storage",
    .name = "sd",
    .init = driver_storage_sd_init,
    .write = driver_storage_sd_write,
    .read = driver_storage_sd_read,
    .shutdown = driver_storage_sd_shutdown};

EOS_DRIVER_REG(driver_storage_sd, EOS_INIT_DRIVERS_PRIO);

#endif