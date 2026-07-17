#include "includes.h"
#ifdef EOS_DRIVER_BUS_SPI_ENABLED

#include "driver/bus/bus.h"
#include "sys/capsmgr.h"
#include "driver/driver.h"
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

bool driver_bus_spi_init(eos_dev_t *dev) {
  spi_state_t *state = malloc(sizeof(spi_state_t));
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
  if (!eos_cap_alloc(EOS_CAPS_SPI, state->host, dev)) {
    free(state);
    return false;
  }

  // Claim pins
  eos_cap_alloc(EOS_CAPS_GPIO, state->bus_cfg.sclk_io_num, dev);
  eos_cap_alloc(EOS_CAPS_GPIO, state->bus_cfg.mosi_io_num, dev);
  eos_cap_alloc(EOS_CAPS_GPIO, state->bus_cfg.miso_io_num, dev);

  esp_err_t err =
      spi_bus_initialize(state->host, &state->bus_cfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    eos_cap_free(EOS_CAPS_SPI, state->host, dev);
    free(state);
    return false;
  }

  return true;
}

// ── ioctl ─────────────────────────────────────────────────────

int driver_bus_spi_ioctl(eos_dev_t *dev_bus, int cmd, ...) {
  va_list args;
  va_start(args, cmd);
  spi_state_t *state = dev_bus->state;

  switch (cmd) {
  case EOS_BUS_IOCTL_KID_ATTACH: {
    // eos_dev_t *child = va_arg(args, eos_dev_t *);

    // // Claim CS pin for child
    // int32_t cs = eos_pin_get_no(child->pins, "cs_ena_pretrans");
    // if (!eos_cap_alloc(EOS_CAPS_GPIO, cs, child)) {
    //   va_end(args);
    //   return false;
    // }

    // // Add device to SPI bus
    // spi_dev_state_t *dev_state = malloc(sizeof(spi_dev_state_t));
    // if (!dev_state) {
    //   eos_cap_free(EOS_CAPS_GPIO, cs, child);
    //   va_end(args);
    //   return false;
    // }

    // dev_state->dev_cfg = (spi_device_interface_config_t){
    //     .clock_speed_hz =
    //         eos_cfg_get_i(child->cfg, "clock_speed_hz", SPI_MASTER_FREQ_8M),
    //     .spics_io_num = cs,
    //     .queue_size = 1,
    // };

    // esp_err_t err = spi_bus_add_device(state->host, &dev_state->dev_cfg,
    //                                    &dev_state->handle);
    // if (err != ESP_OK) {
    //   eos_cap_free(EOS_CAPS_GPIO, cs, child);
    //   free(dev_state);
    //   va_end(args);
    //   return false;
    // }

    // child->state = dev_state;
    // va_end(args);
    return true;
  }

  case EOS_BUS_IOCTL_KID_DETACH: {
    eos_dev_t *child = va_arg(args, eos_dev_t *);
    spi_dev_state_t *dev_state = child->state;
    if (dev_state) {
      spi_bus_remove_device(dev_state->handle);
      eos_cap_free(EOS_CAPS_GPIO, eos_pin_get_no(child->pins, "cs"), child);
      free(dev_state);
      child->state = NULL;
    }
    va_end(args);
    return true;
  }
  }

  va_end(args);
  return 0;
}

void driver_bus_spi_shutdown(eos_dev_t *dev) {
  spi_state_t *state = dev->state;
  if (!state)
    return;

  spi_bus_free(state->host);
  eos_cap_free(EOS_CAPS_SPI, state->host, dev);
  eos_cap_free(EOS_CAPS_GPIO, state->bus_cfg.sclk_io_num, dev);
  eos_cap_free(EOS_CAPS_GPIO, state->bus_cfg.mosi_io_num, dev);
  eos_cap_free(EOS_CAPS_GPIO, state->bus_cfg.miso_io_num, dev);
  free(state);
  dev->state = NULL;
}

EOS_DRIVER_ATTR eos_driver_t driver_bus_spi = {.scope = "bus",
                                               .name = "spi",
                                               .init = driver_bus_spi_init,
                                               .ioctl = driver_bus_spi_ioctl,
                                               .shutdown =
                                                   driver_bus_spi_shutdown};

EOS_DRIVER_REG(driver_bus_spi, EOS_INIT_DRIVERS_BUS);

#endif