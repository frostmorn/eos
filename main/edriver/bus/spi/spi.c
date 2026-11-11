#include "includes.h"
#ifdef EOS_DRV_BUS_SPI_ENABLED

#include "ecore/ioctl.h"
#include "ecore/driver.h"
#include "ecore/capsmgr.h"
#include "emisc/psram_alloc.h"
#include <driver/spi_master.h>

// ── State ─────────────────────────────────────────────────────

typedef struct {
  spi_host_device_t host;
  spi_bus_config_t bus_cfg;
} spi_state_t;

typedef struct {
  spi_device_handle_t handle;
  spi_device_interface_config_t dev_cfg;
} spi_dev_state_t;

// ── Init / Shutdown ───────────────────────────────────────────


// TODO: ioctl based configuration
bool driver_bus_spi_init(eos_dev_t *dev) {
  spi_state_t *state = eos_psram_malloc_prefer(sizeof(spi_state_t));
  if (!state)
    return false;
  dev->state = state;

  state->host = eos_cfg_get_i(dev->cfg, "host", SPI2_HOST);

  state->bus_cfg = (spi_bus_config_t){
      .sclk_io_num = eos_pin_get_no(dev->pins, "sclk"),
      .mosi_io_num = eos_pin_get_no(dev->pins, "mosi"),
      .miso_io_num = eos_pin_get_no(dev->pins, "miso"),
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };

  // Claim SPI peripheral
  if (!eos_cap_claim(EOS_CAPS_SPI, state->host, dev)) {
    free(state);
    return false;
  }

  // Claim pins
  eos_cap_claim(EOS_CAPS_GPIO, state->bus_cfg.sclk_io_num, dev);
  eos_cap_claim(EOS_CAPS_GPIO, state->bus_cfg.mosi_io_num, dev);
  eos_cap_claim(EOS_CAPS_GPIO, state->bus_cfg.miso_io_num, dev);

  esp_err_t err =
      spi_bus_initialize(state->host, &state->bus_cfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    eos_cap_release(EOS_CAPS_SPI, state->host, dev);
    free(state);
    return false;
  }

  return true;
}

// ── ioctl ─────────────────────────────────────────────────────

int driver_bus_spi_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  spi_state_t *state = dev->state;
  if (!state) return -1;

  switch (cmd) {
    case EOS_SPI_IOCTL_GET_HOST: {
      spi_host_device_t *out = va_arg(args, spi_host_device_t *);
      if (out) *out = state->host;
      return 0;
    }
    default:
      return -1;
  }
}

void driver_bus_spi_shutdown(eos_dev_t *dev) {
  spi_state_t *state = dev->state;
  if (!state)
    return;

  spi_bus_free(state->host);
  eos_cap_release(EOS_CAPS_SPI, state->host, dev);
  eos_cap_release(EOS_CAPS_GPIO, state->bus_cfg.sclk_io_num, dev);
  eos_cap_release(EOS_CAPS_GPIO, state->bus_cfg.mosi_io_num, dev);
  eos_cap_release(EOS_CAPS_GPIO, state->bus_cfg.miso_io_num, dev);
  free(state);
  dev->state = NULL;
}

EOS_DRV_ATTR eos_drv_t driver_bus_spi = {EOS_DRV_INIT,
                                         .scope = "bus",
                                         .name = "spi",
                                         .devname = "spi",
                                         .init = driver_bus_spi_init,
                                         .ioctl = driver_bus_spi_ioctl,
                                         .shutdown = driver_bus_spi_shutdown};

#endif
