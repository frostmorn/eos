#include "ecore/usb.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static eos_usb_msc_ops_t s_msc_ops;

void eos_usb_msc_register_ops(const eos_usb_msc_ops_t *ops) {
  if (ops) {
    s_msc_ops = *ops;
  } else {
    memset(&s_msc_ops, 0, sizeof(s_msc_ops));
  }
}

bool __real_tud_msc_test_unit_ready_cb(uint8_t lun);
void __real_tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size);
int32_t __real_tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
int32_t __real_tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);
void __real_tud_msc_inquiry_cb(uint8_t lun, uint8_t *vendor_id, uint8_t *product_id, uint8_t *product_rev);
bool __real_tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject);
int32_t __real_tud_msc_scsi_cb(uint8_t lun, uint8_t const *scsi_cmd, void *buffer, uint16_t bufsize);
uint8_t __real_tud_msc_get_maxlun_cb(void);

bool __wrap_tud_msc_test_unit_ready_cb(uint8_t lun) {
  if (s_msc_ops.test_unit_ready) {
    return s_msc_ops.test_unit_ready(s_msc_ops.ctx, lun);
  }
  return __real_tud_msc_test_unit_ready_cb(lun);
}

void __wrap_tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  if (s_msc_ops.capacity) {
    s_msc_ops.capacity(s_msc_ops.ctx, lun, block_count, block_size);
    return;
  }
  __real_tud_msc_capacity_cb(lun, block_count, block_size);
}

int32_t __wrap_tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (s_msc_ops.read10) {
    return s_msc_ops.read10(s_msc_ops.ctx, lun, lba, offset, buffer, bufsize);
  }
  return __real_tud_msc_read10_cb(lun, lba, offset, buffer, bufsize);
}

int32_t __wrap_tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (s_msc_ops.write10) {
    return s_msc_ops.write10(s_msc_ops.ctx, lun, lba, offset, buffer, bufsize);
  }
  return __real_tud_msc_write10_cb(lun, lba, offset, buffer, bufsize);
}

void __wrap_tud_msc_inquiry_cb(uint8_t lun, uint8_t *vendor_id, uint8_t *product_id, uint8_t *product_rev) {
  if (s_msc_ops.inquiry) {
    s_msc_ops.inquiry(s_msc_ops.ctx, lun, vendor_id, product_id, product_rev);
    return;
  }
  __real_tud_msc_inquiry_cb(lun, vendor_id, product_id, product_rev);
}

bool __wrap_tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  if (s_msc_ops.start_stop) {
    return s_msc_ops.start_stop(s_msc_ops.ctx, lun, power_condition, start, load_eject);
  }
  return __real_tud_msc_start_stop_cb(lun, power_condition, start, load_eject);
}

int32_t __wrap_tud_msc_scsi_cb(uint8_t lun, uint8_t const *scsi_cmd, void *buffer, uint16_t bufsize) {
  if (s_msc_ops.scsi) {
    return s_msc_ops.scsi(s_msc_ops.ctx, lun, scsi_cmd, buffer, bufsize);
  }
  return __real_tud_msc_scsi_cb(lun, scsi_cmd, buffer, bufsize);
}

uint8_t __wrap_tud_msc_get_maxlun_cb(void) {
  if (s_msc_ops.ctx) {
    return 1;
  }
  return __real_tud_msc_get_maxlun_cb();
}
