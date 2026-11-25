#include "includes.h"
#ifdef EOS_DRV_BUS_USB_ENABLED

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/usb.h"
#include "emisc/psram_alloc.h"
#include "edriver/bus/usb/usb.h"

#include <errno.h>
#include <string.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#include <esp_private/usb_phy.h>
#include <tinyusb.h>
#if CONFIG_TINYUSB_CDC_ENABLED
#include <tinyusb_cdc_acm.h>
#endif
#if CONFIG_TINYUSB_MSC_ENABLED
#include <tinyusb_msc.h>
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#endif
#endif

// ── Private Helpers ──────────────────────────────────────────

#if CONFIG_TINYUSB_CDC_ENABLED && defined(CONFIG_IDF_TARGET_ESP32S3)
static void usb_cdc_line_coding_cb(int itf, cdcacm_event_t *event) {
  (void)itf;
  if (event && event->line_coding_changed_data.p_line_coding &&
      event->line_coding_changed_data.p_line_coding->bit_rate == EOS_USB_BOOTLOADER_BAUD) {
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
  }
}
#endif

static void usb_bus_teardown_hw(eos_usb_bus_state_t *st) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
  if (st->tusb_initialized) {
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(EOS_USB_DISCONNECT_SETTLE_MS));
    eos_dev_t *msc_dev = eos_dev_find_by_name("usb_msc0");
    if (msc_dev) {
      char saved_path[EOS_USB_MSC_MAX_BACKING_DEV_LEN];
      strncpy(saved_path, st->msc_backing_dev, sizeof(saved_path) - 1);
      saved_path[sizeof(saved_path) - 1] = '\0';
      eos_dev_ioctl(msc_dev, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, "");
      strncpy(st->msc_backing_dev, saved_path, sizeof(st->msc_backing_dev) - 1);
      st->msc_backing_dev[sizeof(st->msc_backing_dev) - 1] = '\0';
    }
#if CONFIG_TINYUSB_CDC_ENABLED
    if (tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0)) {
      tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    }
#if CONFIG_TINYUSB_CDC_COUNT > 1
    if (tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_1)) {
      tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_1);
    }
#endif
#endif
#if CONFIG_TINYUSB_MSC_ENABLED
    tinyusb_msc_uninstall_driver();
#endif
    tinyusb_driver_uninstall();
    st->tusb_initialized = false;
    st->hw_initialized = false;
  }
  if (st->phy_handle) {
    usb_del_phy((usb_phy_handle_t)st->phy_handle);
    st->phy_handle = NULL;
    st->hw_initialized = false;
  }
#else
  (void)st;
#endif
}

static bool usb_bus_apply_host(eos_usb_bus_state_t *st) {
  st->vbus_present = true;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  st->speed = (st->port_id == 1) ? EOS_USB_SPEED_HIGH : EOS_USB_SPEED_FULL;
#else
  st->speed = EOS_USB_SPEED_FULL;
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
#if defined(CONFIG_IDF_TARGET_ESP32P4)
      .target = (st->port_id == 1) ? USB_PHY_TARGET_UTMI : USB_PHY_TARGET_INT,
      .otg_speed = (st->port_id == 1) ? USB_PHY_SPEED_HIGH : USB_PHY_SPEED_FULL,
#else
      .target = USB_PHY_TARGET_INT,
      .otg_speed = USB_PHY_SPEED_FULL,
#endif
      .otg_mode = USB_OTG_MODE_HOST,
      .ext_io_conf = NULL,
      .otg_io_conf = NULL,
  };
  usb_phy_handle_t h = NULL;
  esp_err_t err = usb_new_phy(&phy_conf, &h);
  if (err != ESP_OK) {
    EOS_LOGE("usb%u: host PHY init failed: %s (%d)",
             (unsigned int)st->port_id, esp_err_to_name(err), (int)err);
    st->vbus_present = false;
    return false;
  }
  st->phy_handle = h;
#endif
  st->hw_initialized = true;
  EOS_LOGI("usb%u: switched to HOST PHY mode", (unsigned int)st->port_id);
  return true;
}

#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED
static void usb_bus_init_tusb_classes(eos_usb_bus_state_t *st) {
#if CONFIG_TINYUSB_CDC_ENABLED
  const tinyusb_config_cdcacm_t acm_cfg = {
      .cdc_port = TINYUSB_CDC_ACM_0,
      .callback_rx = NULL,
      .callback_rx_wanted_char = NULL,
      .callback_line_state_changed = NULL,
#if defined(CONFIG_IDF_TARGET_ESP32S3)
      .callback_line_coding_changed = usb_cdc_line_coding_cb,
#else
      .callback_line_coding_changed = NULL,
#endif
  };
  esp_err_t cdc_err = tinyusb_cdcacm_init(&acm_cfg);
  if (cdc_err != ESP_OK) {
    EOS_LOGW("usb%u: tinyusb_cdcacm_init(0) failed: %s", (unsigned int)st->port_id, esp_err_to_name(cdc_err));
  }
#if CONFIG_TINYUSB_CDC_COUNT > 1
  const tinyusb_config_cdcacm_t acm1_cfg = {
      .cdc_port = TINYUSB_CDC_ACM_1,
      .callback_rx = NULL,
      .callback_rx_wanted_char = NULL,
      .callback_line_state_changed = NULL,
      .callback_line_coding_changed = NULL,
  };
  cdc_err = tinyusb_cdcacm_init(&acm1_cfg);
  if (cdc_err != ESP_OK) {
    EOS_LOGW("usb%u: tinyusb_cdcacm_init(1) failed: %s", (unsigned int)st->port_id, esp_err_to_name(cdc_err));
  }
#endif
#endif
#if CONFIG_TINYUSB_MSC_ENABLED
  const tinyusb_msc_driver_config_t msc_cfg = {
      .user_flags = { .auto_mount_off = 1 },
      .callback = NULL,
      .callback_arg = NULL,
  };
  esp_err_t msc_err = tinyusb_msc_install_driver(&msc_cfg);
  if (msc_err != ESP_OK) {
    EOS_LOGW("usb%u: tinyusb_msc_install_driver failed: %s", (unsigned int)st->port_id, esp_err_to_name(msc_err));
  } else {
    eos_dev_t *msc_dev = eos_dev_find_by_name("usb_msc0");
    if (msc_dev && st->msc_backing_dev[0] != '\0') {
      eos_dev_ioctl(msc_dev, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, st->msc_backing_dev);
    }
  }
#endif
}
#endif

static bool usb_bus_apply_composite(eos_usb_bus_state_t *st) {
  st->vbus_present = true;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  st->speed = (st->port_id == 1) ? EOS_USB_SPEED_HIGH : EOS_USB_SPEED_FULL;
#else
  st->speed = EOS_USB_SPEED_FULL;
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  bool can_install_tusb = (st->port_id == 1);
  tinyusb_port_t tusb_port = TINYUSB_PORT_HIGH_SPEED_0;
#else
  bool can_install_tusb = (st->port_id == 0);
  tinyusb_port_t tusb_port = TINYUSB_PORT_FULL_SPEED_0;
#endif

  if (can_install_tusb) {
    const tinyusb_config_t tusb_cfg = {
        .port = tusb_port,
        .phy = {
            .skip_setup = false,
            .self_powered = false,
            .vbus_monitor_io = -1,
        },
        .task = {
            .size = 8192,
            .priority = 5,
            .xCoreID = 0,
        },
        .descriptor = {
            .device = NULL,
            .qualifier = NULL,
            .string = NULL,
            .full_speed_config = NULL,
            .high_speed_config = NULL,
        },
    };
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err == ESP_OK) {
      st->tusb_initialized = true;
      st->hw_initialized = true;
#if CONFIG_TINYUSB_CDC_ENABLED || CONFIG_TINYUSB_MSC_ENABLED
      usb_bus_init_tusb_classes(st);
#endif
      EOS_LOGI("usb%u: TinyUSB composite device initialized on port %u",
               (unsigned int)st->port_id, (unsigned int)tusb_port);
    } else {
      EOS_LOGE("usb%u: tinyusb_driver_install failed: %s (%d)",
               (unsigned int)st->port_id, esp_err_to_name(err), (int)err);
      st->vbus_present = false;
      return false;
    }
  } else {
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_speed = USB_PHY_SPEED_FULL,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .ext_io_conf = NULL,
        .otg_io_conf = NULL,
    };
    usb_phy_handle_t h = NULL;
    esp_err_t err = usb_new_phy(&phy_conf, &h);
    if (err != ESP_OK) {
      EOS_LOGE("usb%u: composite PHY init failed: %s (%d)",
               (unsigned int)st->port_id, esp_err_to_name(err), (int)err);
      st->vbus_present = false;
      return false;
    }
    st->phy_handle = h;
    st->hw_initialized = true;
  }
#else
  st->hw_initialized = true;
#endif
  EOS_LOGI("usb%u: switched to COMPOSITE GADGET mode (gadgets: 0x%lx)",
           (unsigned int)st->port_id, (unsigned long)st->gadget_funcs_enabled);
  return true;
}

static bool usb_bus_apply_debug(eos_usb_bus_state_t *st) {
  st->vbus_present = true;
  st->speed = EOS_USB_SPEED_FULL;
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_SERIAL_JTAG,
      .target = USB_PHY_TARGET_INT,
      .otg_speed = USB_PHY_SPEED_FULL,
      .otg_mode = USB_PHY_MODE_DEFAULT,
      .ext_io_conf = NULL,
      .otg_io_conf = NULL,
  };
  usb_phy_handle_t h = NULL;
  esp_err_t err = usb_new_phy(&phy_conf, &h);
  if (err != ESP_OK) {
    EOS_LOGE("usb%u: debug PHY init failed: %s (%d)",
             (unsigned int)st->port_id, esp_err_to_name(err), (int)err);
    st->vbus_present = false;
    return false;
  }
  st->phy_handle = h;
#endif
  st->hw_initialized = true;
  EOS_LOGI("usb%u: switched to JTAG+CDC DEBUG mode (303a:1001)", (unsigned int)st->port_id);
  return true;
}

static bool usb_bus_apply_off(eos_usb_bus_state_t *st) {
  st->vbus_present = false;
  st->speed = EOS_USB_SPEED_UNKNOWN;
  st->hw_initialized = true;
  EOS_LOGI("usb%u: switched to OFF mode", (unsigned int)st->port_id);
  return true;
}

static bool usb_bus_apply_role(eos_usb_bus_state_t *st, eos_usb_role_t new_role) {
  if (st->role == new_role && st->hw_initialized) {
    return true;
  }
  eos_usb_role_t prev_role = st->role;
  usb_bus_teardown_hw(st);
  st->role = new_role;

  bool ok = false;
  if (st->role == EOS_USB_ROLE_HOST) {
    ok = usb_bus_apply_host(st);
  } else if (st->role == EOS_USB_ROLE_DEVICE_COMPOSITE) {
    ok = usb_bus_apply_composite(st);
  } else if (st->role == EOS_USB_ROLE_DEBUG_USJ) {
    ok = usb_bus_apply_debug(st);
  } else {
    ok = usb_bus_apply_off(st);
  }

  if (!ok) {
    st->role = prev_role;
    st->hw_initialized = false;
  }
  return ok;
}

// ── Dev Operations ───────────────────────────────────────────

bool driver_bus_usb_init(eos_dev_t *dev) {
  eos_usb_bus_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_bus_state_t));
  if (!st) {
    return false;
  }
  memset(st, 0, sizeof(*st));
  dev->state = st;

  st->bus_mutex = xSemaphoreCreateRecursiveMutex();
  if (!st->bus_mutex) {
    free(st);
    dev->state = NULL;
    return false;
  }

  st->bulk_buf = eos_psram_malloc_prefer(EOS_USB_BUS_BUFFER_SIZE);
  if (st->bulk_buf) {
    st->bulk_buf_len = EOS_USB_BUS_BUFFER_SIZE;
  }

  st->port_id = (uint8_t)eos_cfg_get_i(dev->cfg, "port", EOS_USB_DEFAULT_PORT);

  const char *default_role = "device";
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (st->port_id == 0) {
    default_role = "debug";
  }
#endif
  const char *role_str = eos_cfg_get_s(dev->cfg, "role", (char *)default_role);
  eos_usb_role_t init_role = EOS_USB_ROLE_DEVICE_COMPOSITE;
  if (strcmp(role_str, "debug") == 0 || strcmp(role_str, "jtag") == 0) {
    init_role = EOS_USB_ROLE_DEBUG_USJ;
  } else if (strcmp(role_str, "host") == 0) {
    init_role = EOS_USB_ROLE_HOST;
  } else if (strcmp(role_str, "off") == 0) {
    init_role = EOS_USB_ROLE_OFF;
  }

  st->gadget_funcs_enabled = EOS_USB_DEFAULT_GADGET_FUNCS;
  const char *msc_dev = eos_cfg_get_s(dev->cfg, "msc_dev", (char *)EOS_USB_DEFAULT_MSC_BACKING);
  strncpy(st->msc_backing_dev, msc_dev, sizeof(st->msc_backing_dev) - 1);

  usb_bus_apply_role(st, init_role);

  EOS_LOGI("usb%u: bus initialized (role=%s, msc_dev=%s)",
           (unsigned int)st->port_id, role_str, st->msc_backing_dev);
  return true;
}

void driver_bus_usb_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)dev->state;
  usb_bus_teardown_hw(st);
  if (st->bulk_buf) {
    free(st->bulk_buf);
    st->bulk_buf = NULL;
  }
  if (st->bus_mutex) {
    vSemaphoreDelete(st->bus_mutex);
    st->bus_mutex = NULL;
  }
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb: bus shutdown complete");
}

bool driver_bus_usb_attach_req(eos_dev_t *dev, eos_dev_t *child) {
  if (!dev || !dev->state || !child || !child->driver) {
    return false;
  }
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)dev->state;

  if (strcmp(child->driver->scope, "usb") != 0) {
    EOS_LOGW("usb%u: reject child %s with scope %s",
             (unsigned int)st->port_id, child->driver->name, child->driver->scope);
    return false;
  }

  if (st->child_count >= EOS_USB_MAX_CHILDREN) {
    EOS_LOGE("usb%u: max child count reached", (unsigned int)st->port_id);
    return false;
  }

  st->children[st->child_count++] = child;
  EOS_LOGI("usb%u: attached child function %s (total: %u)",
           (unsigned int)st->port_id, child->driver->name, (unsigned int)st->child_count);
  return true;
}

bool driver_bus_usb_detach_req(eos_dev_t *dev, eos_dev_t *child) {
  if (!dev || !dev->state || !child) {
    return false;
  }
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)dev->state;
  for (uint32_t i = 0; i < st->child_count; i++) {
    if (st->children[i] == child) {
      for (uint32_t j = i; j + 1 < st->child_count; j++) {
        st->children[j] = st->children[j + 1];
      }
      st->child_count--;
      EOS_LOGI("usb%u: detached child %s", (unsigned int)st->port_id, child->driver->name);
      return true;
    }
  }
  return false;
}

// ── VFS Operations ───────────────────────────────────────────

int driver_bus_usb_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_bus_usb_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

ssize_t driver_bus_usb_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !dst) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)dev->state;
  xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);

  size_t to_copy = (size < st->bulk_buf_len) ? size : st->bulk_buf_len;
  if (to_copy > 0 && st->bulk_buf) {
    memcpy(dst, st->bulk_buf, to_copy);
    st->rx_bytes += to_copy;
  } else {
    to_copy = 0;
  }

  xSemaphoreGiveRecursive(st->bus_mutex);
  return (ssize_t)to_copy;
}

ssize_t driver_bus_usb_write(eos_dev_t *dev, int fd, const void *data, size_t size) {
  (void)fd;
  if (!dev || !dev->state || !data) {
    errno = EINVAL;
    return -1;
  }
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)dev->state;
  xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);

  size_t to_copy = (size < st->bulk_buf_len) ? size : st->bulk_buf_len;
  if (to_copy > 0 && st->bulk_buf) {
    memcpy(st->bulk_buf, data, to_copy);
    st->tx_bytes += to_copy;
  } else {
    to_copy = 0;
  }

  xSemaphoreGiveRecursive(st->bus_mutex);
  return (ssize_t)to_copy;
}

static int usb_bus_ioctl_role_status(eos_usb_bus_state_t *st, int cmd, va_list args) {
  if (cmd == EOS_USB_IOCTL_GET_ROLE) {
    eos_usb_role_t *out_role = va_arg(args, eos_usb_role_t *);
    if (!out_role) {
      errno = EINVAL;
      return -1;
    }
    *out_role = st->role;
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_SET_ROLE) {
    eos_usb_role_t new_role = (eos_usb_role_t)va_arg(args, int);
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    bool ok = usb_bus_apply_role(st, new_role);
    xSemaphoreGiveRecursive(st->bus_mutex);
    if (!ok) {
      errno = EIO;
      return -1;
    }
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_GET_STATUS) {
    eos_usb_bus_status_t *out_st = va_arg(args, eos_usb_bus_status_t *);
    if (!out_st) {
      errno = EINVAL;
      return -1;
    }
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    out_st->port_id = st->port_id;
    out_st->active_role = st->role;
    out_st->speed = st->speed;
    out_st->vbus_present = st->vbus_present;
    out_st->host_device_connected = st->host_device_connected;
    out_st->gadget_funcs_enabled = st->gadget_funcs_enabled;
    strncpy(out_st->msc_backing_dev, st->msc_backing_dev, sizeof(out_st->msc_backing_dev) - 1);
    out_st->msc_backing_dev[sizeof(out_st->msc_backing_dev) - 1] = '\0';
    out_st->rx_bytes = st->rx_bytes;
    out_st->tx_bytes = st->tx_bytes;
    xSemaphoreGiveRecursive(st->bus_mutex);
    return 0;
  }
  errno = ENOSYS;
  return -1;
}

static int usb_bus_ioctl_gadget(eos_usb_bus_state_t *st, int cmd, va_list args) {
  if (cmd == EOS_USB_IOCTL_GADGET_SET_FUNCS) {
    uint32_t funcs = va_arg(args, uint32_t);
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    st->gadget_funcs_enabled = funcs;
    xSemaphoreGiveRecursive(st->bus_mutex);
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_GADGET_GET_FUNCS) {
    uint32_t *out_funcs = va_arg(args, uint32_t *);
    if (!out_funcs) {
      errno = EINVAL;
      return -1;
    }
    *out_funcs = st->gadget_funcs_enabled;
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_GADGET_SET_MSC_DEV) {
    const char *msc_path = va_arg(args, const char *);
    if (!msc_path) {
      errno = EINVAL;
      return -1;
    }
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    bool changed = (strcmp(st->msc_backing_dev, msc_path) != 0);
    strncpy(st->msc_backing_dev, msc_path, sizeof(st->msc_backing_dev) - 1);
    st->msc_backing_dev[sizeof(st->msc_backing_dev) - 1] = '\0';
    xSemaphoreGiveRecursive(st->bus_mutex);
    if (changed) {
      eos_dev_t *msc_dev = eos_dev_find_by_name("usb_msc0");
      if (msc_dev) {
        eos_dev_ioctl(msc_dev, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, msc_path);
      }
    }
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_GADGET_GET_MSC_DEV) {
    char *out_path = va_arg(args, char *);
    if (!out_path) {
      errno = EINVAL;
      return -1;
    }
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    strncpy(out_path, st->msc_backing_dev, EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1);
    out_path[EOS_USB_MSC_MAX_BACKING_DEV_LEN - 1] = '\0';
    xSemaphoreGiveRecursive(st->bus_mutex);
    return 0;
  }
  errno = ENOSYS;
  return -1;
}

static int usb_bus_ioctl_host(eos_usb_bus_state_t *st, int cmd, va_list args) {
  if (cmd == EOS_USB_IOCTL_HOST_ENUM) {
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    if (st->role == EOS_USB_ROLE_HOST) {
      st->host_device_connected = true;
      st->host_devs.count = 1;
      st->host_devs.devices[0].dev_addr = 1;
      st->host_devs.devices[0].vid = EOS_USB_DEFAULT_VID;
      st->host_devs.devices[0].pid = EOS_USB_DEFAULT_PID;
      st->host_devs.devices[0].speed = EOS_USB_SPEED_FULL;
      strncpy(st->host_devs.devices[0].class_name, "storage", sizeof(st->host_devs.devices[0].class_name) - 1);
    }
    xSemaphoreGiveRecursive(st->bus_mutex);
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_HOST_RESET_PORT) {
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    st->host_port_resets++;
    xSemaphoreGiveRecursive(st->bus_mutex);
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_HOST_GET_DEV_LIST) {
    eos_usb_host_dev_list_t *out_list = va_arg(args, eos_usb_host_dev_list_t *);
    if (!out_list) {
      errno = EINVAL;
      return -1;
    }
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    memcpy(out_list, &st->host_devs, sizeof(eos_usb_host_dev_list_t));
    xSemaphoreGiveRecursive(st->bus_mutex);
    return 0;
  }
  errno = ENOSYS;
  return -1;
}

int driver_bus_usb_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)dev->state;

  if (cmd >= EOS_USB_IOCTL_GET_ROLE && cmd <= EOS_USB_IOCTL_GET_STATUS) {
    return usb_bus_ioctl_role_status(st, cmd, args);
  }
  if (cmd >= EOS_USB_IOCTL_GADGET_SET_FUNCS && cmd <= EOS_USB_IOCTL_GADGET_GET_MSC_DEV) {
    return usb_bus_ioctl_gadget(st, cmd, args);
  }
  if (cmd >= EOS_USB_IOCTL_HOST_ENUM && cmd <= EOS_USB_IOCTL_HOST_GET_DEV_LIST) {
    return usb_bus_ioctl_host(st, cmd, args);
  }

  if (cmd == EOS_USB_IOCTL_JTAG_SCAN) {
    eos_usb_jtag_pattern_t *pat = va_arg(args, eos_usb_jtag_pattern_t *);
    if (!pat) {
      errno = EINVAL;
      return -1;
    }
    pat->tdo = (pat->tdi ^ pat->tms);
    return 0;
  }

  errno = ENOSYS;
  return -1;
}

EOS_DRV_ATTR eos_drv_t driver_bus_usb = {
    EOS_DRV_INIT,
    .scope = "bus",
    .name = "usb",
    .devname = "usb",
    .init = driver_bus_usb_init,
    .shutdown = driver_bus_usb_shutdown,
    .attach_req = driver_bus_usb_attach_req,
    .detach_req = driver_bus_usb_detach_req,
    .open = driver_bus_usb_open,
    .close = driver_bus_usb_close,
    .read = driver_bus_usb_read,
    .write = driver_bus_usb_write,
    .ioctl = driver_bus_usb_ioctl,
};

#endif
