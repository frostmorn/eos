#include "includes.h"
#ifdef EOS_DRV_BUS_USB_ENABLED

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/fs.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/usb.h"
#include "emisc/psram_alloc.h"
#include "edriver/bus/usb/usb.h"

#include <errno.h>
#include <string.h>
#include <esp_heap_caps.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#include <esp_private/usb_phy.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#endif
#endif

// ── Private Helpers ──────────────────────────────────────────

#ifdef CONFIG_CHERRYUSB_DEVICE
static const uint8_t s_device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(EOS_USB_BCD_VERSION,
                               EOS_USB_DEV_CLASS_MISC,
                               EOS_USB_DEV_SUBCLASS_COMMON,
                               EOS_USB_DEV_PROTOCOL_IAD,
                               EOS_USB_DEFAULT_VID,
                               EOS_USB_DEFAULT_PID,
                               EOS_USB_BCD_DEVICE,
                               EOS_USB_STRID_MFG)
};

static const uint8_t s_device_quality_descriptor[EOS_USB_EP_QUALIFIER_SIZE] = {
    EOS_USB_EP_QUALIFIER_SIZE,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02,
    0x00, 0x00, 0x00,
    EOS_USB_EP_DATA_FS_SIZE,
    0x01, 0x00,
};

static const char *s_string_descriptors[EOS_USB_STRING_DESC_COUNT] = {
    (const char[]){ 0x09, 0x04 },
    EOS_USB_DESC_MANUFACTURER_STR,
    EOS_USB_DESC_PRODUCT_STR,
    EOS_USB_DESC_SERIAL_STR,
    "EOS Serial Console",
    "EOS Virtual Modem",
    "EOS Mass Storage",
    "MTP",
};

static const uint8_t s_wcid_msos_str[EOS_USB_WCID_STR_LEN] = {
    EOS_USB_WCID_STR_LEN, USB_DESCRIPTOR_TYPE_STRING,
    'M', 0x00, 'S', 0x00, 'F', 0x00, 'T', 0x00,
    '1', 0x00, '0', 0x00, '0', 0x00,
    EOS_USB_WCID_VENDOR_CODE, 0x00,
};

static const uint8_t s_wcid_mtp_compat[EOS_USB_WCID_DESC_LEN] = {
    EOS_USB_WCID_DESC_LEN, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x04, 0x00,
    0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    EOS_USB_MTP_ITF_NUM, 0x01,
    'M', 'T', 'P', 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const struct usb_msosv1_descriptor s_msosv1_desc = {
    .string = s_wcid_msos_str,
    .vendor_code = EOS_USB_WCID_VENDOR_CODE,
    .compat_id = s_wcid_mtp_compat,
    .comp_id_property = NULL,
};

static eos_dev_t *usb_bus_get_by_busid(uint8_t busid) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  eos_dev_t *dev = (busid == 0) ? eos_dev_find_by_name("usb1") : eos_dev_find_by_name("usb0");
  if (dev) return dev;
  return eos_dev_find_by_name("usb0");
#else
  (void)busid;
  return eos_dev_find_by_name("usb0");
#endif
}

static eos_usb_bus_state_t *usb_bus_find_active_gadget(uint8_t speed) {
  eos_dev_t *d1 = eos_dev_find_by_name("usb1");
  eos_dev_t *d0 = eos_dev_find_by_name("usb0");
  if (speed == USB_SPEED_HIGH && d1 && d1->state) {
    eos_usb_bus_state_t *st1 = (eos_usb_bus_state_t *)d1->state;
    if (st1->role == EOS_USB_ROLE_DEVICE_COMPOSITE) return st1;
  }
  if (d0 && d0->state) {
    eos_usb_bus_state_t *st0 = (eos_usb_bus_state_t *)d0->state;
    if (st0->role == EOS_USB_ROLE_DEVICE_COMPOSITE) return st0;
  }
  if (d1 && d1->state) {
    eos_usb_bus_state_t *st1 = (eos_usb_bus_state_t *)d1->state;
    if (st1->role == EOS_USB_ROLE_DEVICE_COMPOSITE) return st1;
  }
  return NULL;
}

static uint32_t usb_bus_build_config_desc(eos_usb_bus_state_t *st, uint16_t mps) {
  uint8_t *buf = st->config_desc;
  uint8_t header[EOS_USB_CONFIG_HDR_LEN] = {
      USB_CONFIG_DESCRIPTOR_INIT(0, 0, 1, USB_CONFIG_BUS_POWERED, EOS_USB_CONFIG_POWER_MA)
  };
  memcpy(buf, header, sizeof(header));
  uint32_t offset = sizeof(header);
  uint8_t intf_idx = 0;

  if (st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_SERIAL) {
    const uint8_t cdc0_desc[] = {
        CDC_ACM_DESCRIPTOR_INIT(intf_idx, EOS_USB_EP_CDC0_INT, EOS_USB_EP_CDC0_OUT,
                                EOS_USB_EP_CDC0_IN, mps, EOS_USB_STRID_CDC)
    };
    if (offset + sizeof(cdc0_desc) <= EOS_USB_MAX_CONFIG_DESC_SIZE) {
      memcpy(buf + offset, cdc0_desc, sizeof(cdc0_desc));
      offset += sizeof(cdc0_desc);
      intf_idx += 2;
    }
  }

  if (st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MODEM) {
    const uint8_t cdc1_desc[] = {
        CDC_ACM_DESCRIPTOR_INIT(intf_idx, EOS_USB_EP_CDC1_INT, EOS_USB_EP_CDC1_OUT,
                                EOS_USB_EP_CDC1_IN, mps, EOS_USB_STRID_MODEM)
    };
    if (offset + sizeof(cdc1_desc) <= EOS_USB_MAX_CONFIG_DESC_SIZE) {
      memcpy(buf + offset, cdc1_desc, sizeof(cdc1_desc));
      offset += sizeof(cdc1_desc);
      intf_idx += 2;
    }
  }

  if (st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MTP) {
    const uint8_t mtp_desc[] = {
        MTP_DESCRIPTOR_INIT(intf_idx, EOS_USB_EP_STORAGE_OUT, EOS_USB_EP_STORAGE_IN,
                            EOS_USB_EP_MTP_INT, mps, EOS_USB_STRID_MTP)
    };
    if (offset + sizeof(mtp_desc) <= EOS_USB_MAX_CONFIG_DESC_SIZE) {
      memcpy(buf + offset, mtp_desc, sizeof(mtp_desc));
      offset += sizeof(mtp_desc);
      intf_idx += 1;
    }
  } else if (st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MSC) {
    const uint8_t msc_desc[] = {
        MSC_DESCRIPTOR_INIT(intf_idx, EOS_USB_EP_STORAGE_OUT, EOS_USB_EP_STORAGE_IN,
                            mps, EOS_USB_STRID_MSC)
    };
    if (offset + sizeof(msc_desc) <= EOS_USB_MAX_CONFIG_DESC_SIZE) {
      memcpy(buf + offset, msc_desc, sizeof(msc_desc));
      offset += sizeof(msc_desc);
      intf_idx += 1;
    }
  }

  buf[2] = (uint8_t)offset;
  buf[3] = (uint8_t)(offset >> 8);
  buf[4] = intf_idx;
  st->config_desc_len = offset;
  return offset;
}

static const uint8_t *usb_bus_device_desc_cb(uint8_t speed) {
  (void)speed;
  return s_device_descriptor;
}

static const uint8_t *usb_bus_config_desc_cb(uint8_t speed) {
  eos_usb_bus_state_t *st = usb_bus_find_active_gadget(speed);
  if (!st) return NULL;
  uint16_t mps = (speed == USB_SPEED_HIGH) ? EOS_USB_EP_DATA_HS_SIZE : EOS_USB_EP_DATA_FS_SIZE;
  usb_bus_build_config_desc(st, mps);
  return st->config_desc;
}

static const uint8_t *usb_bus_qual_desc_cb(uint8_t speed) {
  (void)speed;
  return s_device_quality_descriptor;
}

static const char *usb_bus_string_desc_cb(uint8_t speed, uint8_t index) {
  (void)speed;
  if (index >= EOS_USB_STRING_DESC_COUNT) {
    return NULL;
  }
  return s_string_descriptors[index];
}

static const struct usb_descriptor s_cusb_descriptor = {
    .device_descriptor_callback = usb_bus_device_desc_cb,
    .config_descriptor_callback = usb_bus_config_desc_cb,
    .device_quality_descriptor_callback = usb_bus_qual_desc_cb,
    .string_descriptor_callback = usb_bus_string_desc_cb,
    .msosv1_descriptor = &s_msosv1_desc,
};

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding) {
  (void)busid;
  (void)intf;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  if (line_coding && line_coding->dwDTERate == EOS_USB_BOOTLOADER_BAUD) {
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
  }
#else
  (void)line_coding;
#endif
}

static void usb_bus_cdc_rx_dispatch(eos_usb_bus_state_t *st, uint8_t cdc_idx,
                                    uint8_t busid, uint8_t ep, uint8_t *buf, uint32_t nbytes) {
  BaseType_t woken = pdFALSE;
  if (nbytes > 0 && st->cdc_rx_queue) {
    eos_usb_cdc_rx_msg_t msg;
    msg.cdc_index = cdc_idx;
    msg.len = (nbytes > sizeof(msg.data)) ? (uint16_t)sizeof(msg.data) : (uint16_t)nbytes;
    memcpy(msg.data, buf, msg.len);
    xQueueSendFromISR(st->cdc_rx_queue, &msg, &woken);
  }
  st->rx_bytes += nbytes;
  usbd_ep_start_read(busid, ep, buf, EOS_USB_CDC_BUFFER_SIZE);
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

static void usb_bus_cdc0_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  eos_dev_t *bus_dev = usb_bus_get_by_busid(busid);
  if (!bus_dev || !bus_dev->state) return;
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)bus_dev->state;
  usb_bus_cdc_rx_dispatch(st, 0, busid, ep, st->cdc0_rx_buf, nbytes);
}

static void usb_bus_cdc1_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  eos_dev_t *bus_dev = usb_bus_get_by_busid(busid);
  if (!bus_dev || !bus_dev->state) return;
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)bus_dev->state;
  usb_bus_cdc_rx_dispatch(st, 1, busid, ep, st->cdc1_rx_buf, nbytes);
}

static void usb_bus_cdc_in_done(eos_usb_bus_state_t *st, uint8_t cdc_idx,
                                uint8_t busid, uint8_t ep, uint32_t nbytes) {
  if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
    usbd_ep_start_write(busid, ep, NULL, 0);
    return;
  }
  BaseType_t woken = pdFALSE;
  if (cdc_idx == 0) {
    st->cdc0_tx_busy = false;
    if (st->cdc0_tx_sem) xSemaphoreGiveFromISR(st->cdc0_tx_sem, &woken);
  } else {
    st->cdc1_tx_busy = false;
    if (st->cdc1_tx_sem) xSemaphoreGiveFromISR(st->cdc1_tx_sem, &woken);
  }
  if (woken) {
    portYIELD_FROM_ISR();
  }
}

static void usb_bus_cdc0_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  eos_dev_t *bus_dev = usb_bus_get_by_busid(busid);
  if (!bus_dev || !bus_dev->state) return;
  usb_bus_cdc_in_done((eos_usb_bus_state_t *)bus_dev->state, 0, busid, ep, nbytes);
}

static void usb_bus_cdc1_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  eos_dev_t *bus_dev = usb_bus_get_by_busid(busid);
  if (!bus_dev || !bus_dev->state) return;
  usb_bus_cdc_in_done((eos_usb_bus_state_t *)bus_dev->state, 1, busid, ep, nbytes);
}

static void usb_bus_rx_task(void *arg) {
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)arg;
  eos_usb_cdc_rx_msg_t msg;
  while (st && st->cdc_rx_queue) {
    if (xQueueReceive(st->cdc_rx_queue, &msg, portMAX_DELAY) == pdTRUE) {
      const char *target = (msg.cdc_index == 0) ? "usb_serial0" : "usb_modem0";
      eos_dev_t *child = eos_dev_find_by_name(target);
      if (child) {
        eos_dev_ioctl(child, EOS_DRV_FD, EOS_USB_IOCTL_CDC_PUSH_RX, msg.data, (size_t)msg.len);
      }
    }
  }
  vTaskDelete(NULL);
}

static void usb_bus_cusb_event_handler(uint8_t busid, uint8_t event) {
  eos_dev_t *bus_dev = usb_bus_get_by_busid(busid);
  if (!bus_dev || !bus_dev->state) return;
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)bus_dev->state;

  switch (event) {
    case USBD_EVENT_CONFIGURED:
      st->usb_configured = true;
      st->cdc0_tx_busy = false;
      st->cdc1_tx_busy = false;
      if ((st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_SERIAL) && st->cdc0_rx_buf) {
        usbd_ep_start_read(busid, EOS_USB_EP_CDC0_OUT, st->cdc0_rx_buf, EOS_USB_CDC_BUFFER_SIZE);
      }
      if ((st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MODEM) && st->cdc1_rx_buf) {
        usbd_ep_start_read(busid, EOS_USB_EP_CDC1_OUT, st->cdc1_rx_buf, EOS_USB_CDC_BUFFER_SIZE);
      }
      break;
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
      st->usb_configured = false;
      st->cdc0_tx_busy = false;
      st->cdc1_tx_busy = false;
      break;
    default:
      break;
  }
}

static int usb_bus_cdc_send(eos_usb_bus_state_t *st, uint8_t cdc_idx,
                            const void *data, size_t size, size_t *out_sent) {
  if (!st || !st->cusb_initialized || !st->usb_configured || !data || size == 0) {
    if (out_sent) *out_sent = 0;
    return -1;
  }
  uint8_t ep = (cdc_idx == 0) ? EOS_USB_EP_CDC0_IN : EOS_USB_EP_CDC1_IN;
  uint8_t *tx_buf = (cdc_idx == 0) ? st->cdc0_tx_buf : st->cdc1_tx_buf;
  SemaphoreHandle_t sem = (cdc_idx == 0) ? st->cdc0_tx_sem : st->cdc1_tx_sem;
  volatile bool *busy = (cdc_idx == 0) ? &st->cdc0_tx_busy : &st->cdc1_tx_busy;
  if (!tx_buf || !sem) {
    if (out_sent) *out_sent = 0;
    return -1;
  }

  size_t to_send = (size > EOS_USB_CDC_BUFFER_SIZE) ? EOS_USB_CDC_BUFFER_SIZE : size;
  if (*busy) {
    xSemaphoreTake(sem, pdMS_TO_TICKS(EOS_USB_TX_TIMEOUT_MS));
  }
  if (*busy) {
    if (out_sent) *out_sent = 0;
    errno = EBUSY;
    return -1;
  }

  memcpy(tx_buf, data, to_send);
  *busy = true;
  if (usbd_ep_start_write(st->bus_id, ep, tx_buf, (uint32_t)to_send) < 0) {
    *busy = false;
    if (out_sent) *out_sent = 0;
    return -1;
  }
  xSemaphoreTake(sem, pdMS_TO_TICKS(EOS_USB_TX_TIMEOUT_MS));
  st->tx_bytes += (uint32_t)to_send;
  if (out_sent) *out_sent = to_send;
  return 0;
}
#endif

static void usb_bus_teardown_hw(eos_usb_bus_state_t *st) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#ifdef CONFIG_CHERRYUSB_DEVICE
  if (st->cusb_initialized) {
    usbd_deinitialize(st->bus_id);
    st->cusb_initialized = false;
    st->usb_configured = false;
    st->mtp_initialized = false;
    st->hw_initialized = false;
  }
#endif
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

static eos_dev_t *usb_bus_find_child_by_drv(eos_usb_bus_state_t *st, const char *drv_name,
                                           const char *dev_name) {
  for (uint32_t i = 0; i < st->child_count; i++) {
    if (st->children[i] && st->children[i]->driver &&
        strcmp(st->children[i]->driver->name, drv_name) == 0) {
      return st->children[i];
    }
  }
  return eos_dev_find_by_name(dev_name);
}

static bool usb_bus_apply_composite(eos_usb_bus_state_t *st) {
  st->vbus_present = true;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  st->speed = (st->port_id == 1) ? EOS_USB_SPEED_HIGH : EOS_USB_SPEED_FULL;
#else
  st->speed = EOS_USB_SPEED_FULL;
#endif

#ifdef CONFIG_CHERRYUSB_DEVICE
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  st->bus_id = (st->port_id == 1) ? 0 : 1;
  uintptr_t reg_base = (st->port_id == 1) ? ESP_USB_HS0_BASE : ESP_USB_FS0_BASE;
#else
  st->bus_id = 0;
  uintptr_t reg_base = ESP_USB_FS0_BASE;
#endif

  usbd_desc_register(st->bus_id, &s_cusb_descriptor);

  usbd_add_interface(st->bus_id, usbd_cdc_acm_init_intf(st->bus_id, &st->intf_cdc0_ctrl));
  usbd_add_interface(st->bus_id, usbd_cdc_acm_init_intf(st->bus_id, &st->intf_cdc0_data));
  st->ep_cdc0_out.ep_addr = EOS_USB_EP_CDC0_OUT;
  st->ep_cdc0_out.ep_cb = usb_bus_cdc0_out_cb;
  st->ep_cdc0_in.ep_addr = EOS_USB_EP_CDC0_IN;
  st->ep_cdc0_in.ep_cb = usb_bus_cdc0_in_cb;
  usbd_add_endpoint(st->bus_id, &st->ep_cdc0_out);
  usbd_add_endpoint(st->bus_id, &st->ep_cdc0_in);

  usbd_add_interface(st->bus_id, usbd_cdc_acm_init_intf(st->bus_id, &st->intf_cdc1_ctrl));
  usbd_add_interface(st->bus_id, usbd_cdc_acm_init_intf(st->bus_id, &st->intf_cdc1_data));
  st->ep_cdc1_out.ep_addr = EOS_USB_EP_CDC1_OUT;
  st->ep_cdc1_out.ep_cb = usb_bus_cdc1_out_cb;
  st->ep_cdc1_in.ep_addr = EOS_USB_EP_CDC1_IN;
  st->ep_cdc1_in.ep_cb = usb_bus_cdc1_in_cb;
  usbd_add_endpoint(st->bus_id, &st->ep_cdc1_out);
  usbd_add_endpoint(st->bus_id, &st->ep_cdc1_in);

  usbd_add_interface(st->bus_id, &st->intf_storage);

  if (st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MTP) {
    eos_dev_t *mtp_dev = usb_bus_find_child_by_drv(st, "mtp", "usb_mtp0");
    if (mtp_dev) {
      const char *path = st->mtp_base_path[0] ? st->mtp_base_path : EOS_USB_DEFAULT_MTP_PATH;
      eos_dev_ioctl(mtp_dev, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MTP_PATH, path);
      eos_dev_ioctl(mtp_dev, EOS_DRV_FD, EOS_USB_IOCTL_MTP_ATTACH_BUS,
                    (uint32_t)st->bus_id,
                    &st->intf_storage,
                    (uint32_t)EOS_USB_EP_STORAGE_OUT,
                    (uint32_t)EOS_USB_EP_STORAGE_IN,
                    (uint32_t)EOS_USB_EP_MTP_INT);
      st->mtp_initialized = true;
    }
  } else if (st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MSC) {
    usbd_msc_init_intf(st->bus_id, &st->intf_storage,
                       EOS_USB_EP_STORAGE_OUT, EOS_USB_EP_STORAGE_IN);
    eos_dev_t *msc_dev = usb_bus_find_child_by_drv(st, "msc", "usb_msc0");
    if (msc_dev && st->msc_backing_dev[0] != '\0') {
      eos_dev_ioctl(msc_dev, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, st->msc_backing_dev);
    }
  }

  if (usbd_initialize(st->bus_id, reg_base, usb_bus_cusb_event_handler) != 0) {
    EOS_LOGE("usb%u: CherryUSB usbd_initialize failed", (unsigned int)st->port_id);
    st->vbus_present = false;
    return false;
  }
  st->cusb_initialized = true;
  st->hw_initialized = true;
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
    if (prev_role == EOS_USB_ROLE_HOST) {
      st->hw_initialized = usb_bus_apply_host(st);
    } else if (prev_role == EOS_USB_ROLE_DEVICE_COMPOSITE) {
      st->hw_initialized = usb_bus_apply_composite(st);
    } else if (prev_role == EOS_USB_ROLE_DEBUG_USJ) {
      st->hw_initialized = usb_bus_apply_debug(st);
    }
    if (!st->hw_initialized) {
      st->role = EOS_USB_ROLE_OFF;
      st->hw_initialized = usb_bus_apply_off(st);
    }
  }
  return ok;
}

#ifdef CONFIG_CHERRYUSB_DEVICE
static bool usb_bus_alloc_cdc_resources(eos_usb_bus_state_t *st) {
  st->cdc0_rx_buf = heap_caps_aligned_alloc(EOS_USB_DMA_ALIGN, EOS_USB_CDC_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  st->cdc0_tx_buf = heap_caps_aligned_alloc(EOS_USB_DMA_ALIGN, EOS_USB_CDC_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  st->cdc1_rx_buf = heap_caps_aligned_alloc(EOS_USB_DMA_ALIGN, EOS_USB_CDC_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  st->cdc1_tx_buf = heap_caps_aligned_alloc(EOS_USB_DMA_ALIGN, EOS_USB_CDC_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  st->cdc0_tx_sem = xSemaphoreCreateBinary();
  st->cdc1_tx_sem = xSemaphoreCreateBinary();
  st->cdc_rx_queue = xQueueCreate(EOS_USB_CDC_RX_QUEUE_LEN, sizeof(eos_usb_cdc_rx_msg_t));
  if (!st->cdc0_rx_buf || !st->cdc0_tx_buf || !st->cdc1_rx_buf || !st->cdc1_tx_buf ||
      !st->cdc0_tx_sem || !st->cdc1_tx_sem || !st->cdc_rx_queue) {
    return false;
  }
  xTaskCreate(usb_bus_rx_task, "usb_rx", EOS_USB_RX_TASK_STACK, st, EOS_USB_RX_TASK_PRIO, &st->cdc_rx_task);
  return true;
}

static void usb_bus_free_cdc_resources(eos_usb_bus_state_t *st) {
  if (st->cdc_rx_task) {
    vTaskDelete(st->cdc_rx_task);
    st->cdc_rx_task = NULL;
  }
  if (st->cdc_rx_queue) {
    vQueueDelete(st->cdc_rx_queue);
    st->cdc_rx_queue = NULL;
  }
  if (st->cdc0_tx_sem) {
    vSemaphoreDelete(st->cdc0_tx_sem);
    st->cdc0_tx_sem = NULL;
  }
  if (st->cdc1_tx_sem) {
    vSemaphoreDelete(st->cdc1_tx_sem);
    st->cdc1_tx_sem = NULL;
  }
  if (st->cdc0_rx_buf) free(st->cdc0_rx_buf);
  if (st->cdc0_tx_buf) free(st->cdc0_tx_buf);
  if (st->cdc1_rx_buf) free(st->cdc1_rx_buf);
  if (st->cdc1_tx_buf) free(st->cdc1_tx_buf);
}
#endif

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

#ifdef CONFIG_CHERRYUSB_DEVICE
  if (!usb_bus_alloc_cdc_resources(st)) {
    usb_bus_free_cdc_resources(st);
    if (st->bulk_buf) free(st->bulk_buf);
    vSemaphoreDelete(st->bus_mutex);
    free(st);
    dev->state = NULL;
    return false;
  }
#endif

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
  const char *mtp_path = eos_cfg_get_s(dev->cfg, "mtp_path", (char *)EOS_USB_DEFAULT_MTP_PATH);
  strncpy(st->mtp_base_path, mtp_path, sizeof(st->mtp_base_path) - 1);

  usb_bus_apply_role(st, init_role);

  EOS_LOGI("usb%u: bus initialized (role=%s, msc_dev=%s, mtp_path=%s)",
           (unsigned int)st->port_id, role_str, st->msc_backing_dev, st->mtp_base_path);
  return true;
}

void driver_bus_usb_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) {
    return;
  }
  eos_usb_bus_state_t *st = (eos_usb_bus_state_t *)dev->state;
  usb_bus_teardown_hw(st);
#ifdef CONFIG_CHERRYUSB_DEVICE
  usb_bus_free_cdc_resources(st);
#endif
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

  if (strcmp(child->driver->name, "mtp") == 0) {
    const char *child_path = eos_cfg_get_s(child->cfg, "path", NULL);
    const char *path = (child_path && child_path[0]) ? child_path :
                       (st->mtp_base_path[0] ? st->mtp_base_path : EOS_USB_DEFAULT_MTP_PATH);
    strncpy(st->mtp_base_path, path, sizeof(st->mtp_base_path) - 1);
    st->mtp_base_path[sizeof(st->mtp_base_path) - 1] = '\0';
    eos_dev_ioctl(child, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MTP_PATH, path);
    if (st->role == EOS_USB_ROLE_DEVICE_COMPOSITE && (st->gadget_funcs_enabled & EOS_USB_GADGET_FUNC_MTP)) {
      usb_bus_teardown_hw(st);
      usb_bus_apply_composite(st);
    }
  } else if (strcmp(child->driver->name, "msc") == 0) {
    const char *dev_path = st->msc_backing_dev[0] ? st->msc_backing_dev : EOS_USB_DEFAULT_MSC_BACKING;
    eos_dev_ioctl(child, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MSC_DEV, dev_path);
  }

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
    strncpy(out_st->mtp_base_path, st->mtp_base_path, sizeof(out_st->mtp_base_path) - 1);
    out_st->mtp_base_path[sizeof(out_st->mtp_base_path) - 1] = '\0';
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
    uint32_t old_funcs = st->gadget_funcs_enabled;
    st->gadget_funcs_enabled = funcs;
    xSemaphoreGiveRecursive(st->bus_mutex);

    if (old_funcs != funcs && st->role == EOS_USB_ROLE_DEVICE_COMPOSITE) {
      if ((funcs & EOS_USB_GADGET_FUNC_MSC) && !(old_funcs & EOS_USB_GADGET_FUNC_MSC)) {
        eos_umount("/mnt/sd", 0);
      }
      usb_bus_teardown_hw(st);
      usb_bus_apply_composite(st);
      if ((funcs & EOS_USB_GADGET_FUNC_MTP) && !(old_funcs & EOS_USB_GADGET_FUNC_MTP)) {
        eos_mount("/dev/sd0", "/mnt/sd", "vfat", 0, NULL);
      }
    }
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
  if (cmd == EOS_USB_IOCTL_GADGET_SET_MTP_PATH) {
    const char *mtp_path = va_arg(args, const char *);
    if (!mtp_path) {
      errno = EINVAL;
      return -1;
    }
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    strncpy(st->mtp_base_path, mtp_path, sizeof(st->mtp_base_path) - 1);
    st->mtp_base_path[sizeof(st->mtp_base_path) - 1] = '\0';
    xSemaphoreGiveRecursive(st->bus_mutex);
    eos_dev_t *mtp_dev = eos_dev_find_by_name("usb_mtp0");
    if (mtp_dev) {
      eos_dev_ioctl(mtp_dev, EOS_DRV_FD, EOS_USB_IOCTL_GADGET_SET_MTP_PATH, mtp_path);
    }
    return 0;
  }
  if (cmd == EOS_USB_IOCTL_GADGET_GET_MTP_PATH) {
    char *out_path = va_arg(args, char *);
    if (!out_path) {
      errno = EINVAL;
      return -1;
    }
    xSemaphoreTakeRecursive(st->bus_mutex, portMAX_DELAY);
    strncpy(out_path, st->mtp_base_path, EOS_USB_MTP_MAX_PATH_LEN - 1);
    out_path[EOS_USB_MTP_MAX_PATH_LEN - 1] = '\0';
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
  if (cmd >= EOS_USB_IOCTL_GADGET_SET_FUNCS && cmd <= EOS_USB_IOCTL_GADGET_GET_MTP_PATH) {
    return usb_bus_ioctl_gadget(st, cmd, args);
  }
  if (cmd >= EOS_USB_IOCTL_HOST_ENUM && cmd <= EOS_USB_IOCTL_HOST_GET_DEV_LIST) {
    return usb_bus_ioctl_host(st, cmd, args);
  }

  if (cmd == EOS_USB_IOCTL_CDC_SEND) {
#ifdef CONFIG_CHERRYUSB_DEVICE
    uint8_t cdc_idx = (uint8_t)va_arg(args, int);
    const void *buf = va_arg(args, const void *);
    size_t len = va_arg(args, size_t);
    size_t *out_sent = va_arg(args, size_t *);
    return usb_bus_cdc_send(st, cdc_idx, buf, len, out_sent);
#else
    errno = ENOSYS;
    return -1;
#endif
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
