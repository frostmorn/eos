#include "includes.h"
#ifdef EOS_DRV_USB_MTP_ENABLED

#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/vfs.h"
#include "emisc/psram_alloc.h"
#include "edriver/usb/mtp/mtp.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <esp_heap_caps.h>

#ifdef CONFIG_CHERRYUSB_DEVICE

// ── Binary Encoding Helpers ──────────────────────────────────

static size_t mtp_put_u16(uint8_t *buf, size_t pos, uint16_t v) {
  buf[pos++] = (uint8_t)v;
  buf[pos++] = (uint8_t)(v >> 8);
  return pos;
}

static size_t mtp_put_u32(uint8_t *buf, size_t pos, uint32_t v) {
  buf[pos++] = (uint8_t)v;
  buf[pos++] = (uint8_t)(v >> 8);
  buf[pos++] = (uint8_t)(v >> 16);
  buf[pos++] = (uint8_t)(v >> 24);
  return pos;
}

static size_t mtp_put_u64(uint8_t *buf, size_t pos, uint64_t v) {
  pos = mtp_put_u32(buf, pos, (uint32_t)v);
  pos = mtp_put_u32(buf, pos, (uint32_t)(v >> 32));
  return pos;
}

static size_t mtp_put_str(uint8_t *buf, size_t pos, const char *s) {
  if (!s || s[0] == '\0') {
    buf[pos++] = 0;
    return pos;
  }
  size_t len = strlen(s);
  if (len > EOS_USB_MTP_STR_LEN) len = EOS_USB_MTP_STR_LEN;
  buf[pos++] = (uint8_t)(len + 1);
  for (size_t i = 0; i < len; i++) {
    pos = mtp_put_u16(buf, pos, (uint16_t)(uint8_t)s[i]);
  }
  pos = mtp_put_u16(buf, pos, 0);
  return pos;
}

static uint16_t mtp_get_u16(const uint8_t *buf) {
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t mtp_get_u32(const uint8_t *buf) {
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static size_t mtp_get_str(const uint8_t *buf, size_t max_len, char *out, size_t out_sz) {
  if (max_len < 1 || out_sz == 0) return 0;
  uint8_t num_chars = buf[0];
  if (num_chars == 0) {
    out[0] = '\0';
    return 1;
  }
  size_t byte_len = (size_t)num_chars * 2 + 1;
  if (byte_len > max_len) return 0;
  size_t out_idx = 0;
  for (uint8_t i = 0; i < num_chars; i++) {
    uint16_t ch = mtp_get_u16(buf + 1 + i * 2);
    if (ch == 0) break;
    if (out_idx + 1 < out_sz) {
      out[out_idx++] = (char)(ch & EOS_USB_MTP_ASCII_MASK);
    }
  }
  out[out_idx] = '\0';
  return byte_len;
}

// ── Path Safety & Object Handle Cache ────────────────────────

static bool mtp_is_numeric_name(const char *name) {
  if (!name || name[0] == '\0') return false;
  for (size_t i = 0; name[i] != '\0'; i++) {
    if (!isdigit((unsigned char)name[i])) return false;
  }
  return true;
}

static bool mtp_should_skip_entry(const char *parent_path, const char *name) {
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return true;
  if (strcmp(parent_path, "/") == 0) {
    if (strcmp(name, "dev") == 0 || strcmp(name, "pipes") == 0) return true;
    if (mtp_is_numeric_name(name)) return true;
  }
  return false;
}

static bool mtp_is_protected_path(const char *path) {
  if (!path) return true;
  if (strcmp(path, "/") == 0 ||
      strncmp(path, "/proc", 5) == 0 ||
      strncmp(path, "/bin", 4) == 0 ||
      strncmp(path, "/dev", 4) == 0 ||
      strncmp(path, "/pipes", 6) == 0) {
    return true;
  }
  return false;
}

static eos_usb_mtp_obj_t *mtp_find_by_handle(eos_usb_mtp_state_t *st, uint32_t handle) {
  if (!st || !st->handles || handle == EOS_USB_MTP_ROOT_HANDLE) return NULL;
  for (size_t i = 0; i < st->handle_count; i++) {
    if (st->handles[i].in_use && st->handles[i].handle == handle) {
      return &st->handles[i];
    }
  }
  return NULL;
}

static eos_usb_mtp_obj_t *mtp_find_by_path(eos_usb_mtp_state_t *st, const char *path) {
  if (!st || !st->handles || !path) return NULL;
  for (size_t i = 0; i < st->handle_count; i++) {
    if (st->handles[i].in_use && strcmp(st->handles[i].path, path) == 0) {
      return &st->handles[i];
    }
  }
  return NULL;
}

static eos_usb_mtp_obj_t *mtp_alloc_handle(eos_usb_mtp_state_t *st, uint32_t parent_handle,
                                           const char *full_path, bool is_dir, uint32_t size) {
  eos_usb_mtp_obj_t *existing = mtp_find_by_path(st, full_path);
  if (existing) {
    existing->is_dir = is_dir;
    existing->size = size;
    existing->format = is_dir ? MTP_FORMAT_ASSOCIATION : MTP_FORMAT_UNDEFINED;
    existing->read_only = mtp_is_protected_path(full_path);
    return existing;
  }
  if (st->handle_count >= EOS_USB_MTP_MAX_HANDLES) return NULL;
  eos_usb_mtp_obj_t *obj = &st->handles[st->handle_count++];
  memset(obj, 0, sizeof(*obj));
  obj->handle = st->next_handle++;
  obj->parent_handle = parent_handle;
  obj->is_dir = is_dir;
  obj->size = size;
  obj->format = is_dir ? MTP_FORMAT_ASSOCIATION : MTP_FORMAT_UNDEFINED;
  obj->read_only = mtp_is_protected_path(full_path);
  obj->in_use = true;
  strncpy(obj->path, full_path, sizeof(obj->path) - 1);
  return obj;
}

static const char *mtp_get_parent_path(eos_usb_mtp_state_t *st, uint32_t parent_handle) {
  if (parent_handle == EOS_USB_MTP_ROOT_HANDLE || parent_handle == EOS_USB_MTP_ALL_HANDLES) {
    return st->path_spec[0] ? st->path_spec : EOS_USB_DEFAULT_MTP_PATH;
  }
  eos_usb_mtp_obj_t *obj = mtp_find_by_handle(st, parent_handle);
  return obj ? obj->path : NULL;
}

static void mtp_join_path(char *dst, size_t dst_sz, const char *dir, const char *name) {
  if (!dst || dst_sz == 0) return;
  size_t pos = 0;
  if (strcmp(dir, "/") != 0) {
    size_t dlen = strlen(dir);
    if (dlen >= dst_sz) dlen = dst_sz - 1;
    memcpy(dst, dir, dlen);
    pos = dlen;
  }
  if (pos + 1 < dst_sz) {
    dst[pos++] = '/';
  }
  size_t nlen = strlen(name);
  if (pos + nlen >= dst_sz) {
    nlen = (dst_sz > pos + 1) ? (dst_sz - pos - 1) : 0;
  }
  if (nlen > 0) {
    memcpy(dst + pos, name, nlen);
    pos += nlen;
  }
  dst[pos] = '\0';
}

static size_t mtp_scan_dir(eos_usb_mtp_state_t *st, uint32_t parent_handle,
                           uint32_t *out_handles, size_t max_handles) {
  const char *dir_path = mtp_get_parent_path(st, parent_handle);
  if (!dir_path) return 0;

  DIR *d = opendir(dir_path);
  if (!d) return 0;

  size_t count = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL && count < max_handles) {
    if (mtp_should_skip_entry(dir_path, ent->d_name)) continue;

    char full_path[EOS_USB_MTP_MAX_PATH_LEN];
    mtp_join_path(full_path, sizeof(full_path), dir_path, ent->d_name);

    struct stat sb;
    bool is_dir = (ent->d_type == DT_DIR);
    uint32_t sz = 0;
    if (stat(full_path, &sb) == 0) {
      is_dir = S_ISDIR(sb.st_mode);
      sz = is_dir ? 0 : (uint32_t)sb.st_size;
    }

    uint32_t stored_parent = (parent_handle == EOS_USB_MTP_ALL_HANDLES) ?
                             EOS_USB_MTP_ROOT_HANDLE : parent_handle;
    eos_usb_mtp_obj_t *obj = mtp_alloc_handle(st, stored_parent, full_path, is_dir, sz);
    if (obj && out_handles) {
      out_handles[count++] = obj->handle;
    } else if (obj) {
      count++;
    }
  }
  closedir(d);
  return count;
}

// ── USB Bulk Transport Helpers ───────────────────────────────

static bool mtp_ep_write_sync(eos_usb_mtp_state_t *st, const void *buf, uint32_t len) {
  if (!st->configured) return false;
  if (len > 0) {
    memcpy(st->tx_buf, buf, len);
  }
  if (usbd_ep_start_write(st->bus_id, st->in_ep, st->tx_buf, len) < 0) {
    return false;
  }
  return xSemaphoreTake(st->tx_sem, pdMS_TO_TICKS(EOS_USB_MTP_TX_TIMEOUT_MS)) == pdTRUE;
}

static void mtp_send_container_hdr(uint8_t *buf, uint32_t total_len, uint16_t type,
                                   uint16_t code, uint32_t tx_id) {
  mtp_put_u32(buf, 0, total_len);
  mtp_put_u16(buf, 4, type);
  mtp_put_u16(buf, 6, code);
  mtp_put_u32(buf, 8, tx_id);
}

static void mtp_send_data_packet(eos_usb_mtp_state_t *st, uint16_t code, uint32_t tx_id,
                                 const uint8_t *payload, size_t payload_len) {
  uint32_t total_len = EOS_USB_MTP_CONTAINER_HDR_LEN + (uint32_t)payload_len;
  mtp_send_container_hdr(st->tx_buf, total_len, MTP_CONTAINER_TYPE_DATA, code, tx_id);
  if (payload_len > 0 && payload != st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN) {
    memcpy(st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN, payload, payload_len);
  }
  if (usbd_ep_start_write(st->bus_id, st->in_ep, st->tx_buf, total_len) == 0) {
    xSemaphoreTake(st->tx_sem, pdMS_TO_TICKS(EOS_USB_MTP_TX_TIMEOUT_MS));
  }
  uint16_t mps = usbd_get_ep_mps(st->bus_id, st->in_ep);
  if (mps > 0 && (total_len % mps) == 0) {
    mtp_ep_write_sync(st, NULL, 0);
  }
}

static void mtp_send_response(eos_usb_mtp_state_t *st, uint16_t resp_code, uint32_t tx_id,
                              const uint32_t *params, uint8_t param_count) {
  uint32_t total_len = EOS_USB_MTP_CONTAINER_HDR_LEN + (uint32_t)param_count * 4;
  mtp_send_container_hdr(st->tx_buf, total_len, MTP_CONTAINER_TYPE_RESPONSE, resp_code, tx_id);
  for (uint8_t i = 0; i < param_count; i++) {
    mtp_put_u32(st->tx_buf, EOS_USB_MTP_CONTAINER_HDR_LEN + i * 4, params[i]);
  }
  if (usbd_ep_start_write(st->bus_id, st->in_ep, st->tx_buf, total_len) == 0) {
    xSemaphoreTake(st->tx_sem, pdMS_TO_TICKS(EOS_USB_MTP_TX_TIMEOUT_MS));
  }
}

static bool mtp_wait_rx_packet(eos_usb_mtp_state_t *st, uint32_t *out_nbytes) {
  st->rx_nbytes = 0;
  usbd_ep_start_read(st->bus_id, st->out_ep, st->rx_buf, EOS_USB_MTP_BUF_SIZE);
  if (xSemaphoreTake(st->rx_sem, pdMS_TO_TICKS(EOS_USB_MTP_TX_TIMEOUT_MS)) != pdTRUE) {
    return false;
  }
  if (out_nbytes) *out_nbytes = st->rx_nbytes;
  return true;
}

// ── MTP Operation Handlers ───────────────────────────────────

static void mtp_op_get_device_info(eos_usb_mtp_state_t *st, uint32_t tx_id) {
  static const uint16_t ops[] = {
      MTP_OPERATION_GET_DEVICE_INFO, MTP_OPERATION_OPEN_SESSION, MTP_OPERATION_CLOSE_SESSION,
      MTP_OPERATION_GET_STORAGE_IDS, MTP_OPERATION_GET_STORAGE_INFO, MTP_OPERATION_GET_NUM_OBJECTS,
      MTP_OPERATION_GET_OBJECT_HANDLES, MTP_OPERATION_GET_OBJECT_INFO, MTP_OPERATION_GET_OBJECT,
      MTP_OPERATION_DELETE_OBJECT, MTP_OPERATION_SEND_OBJECT_INFO, MTP_OPERATION_SEND_OBJECT,
      MTP_OPERATION_GET_DEVICE_PROP_DESC, MTP_OPERATION_GET_DEVICE_PROP_VALUE,
      MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED, MTP_OPERATION_GET_OBJECT_PROP_VALUE,
      MTP_OPERATION_GET_OBJECT_PROP_LIST, MTP_OPERATION_GET_OBJECT_REFERENCES
  };
  static const uint16_t fmts[] = { MTP_FORMAT_UNDEFINED, MTP_FORMAT_ASSOCIATION, MTP_FORMAT_TEXT };

  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = 0;
  pos = mtp_put_u16(p, pos, MTP_STANDARD_VERSION);
  pos = mtp_put_u32(p, pos, EOS_USB_MTP_VENDOR_EXT_ID);
  pos = mtp_put_u16(p, pos, EOS_USB_MTP_VENDOR_EXT_VER);
  pos = mtp_put_str(p, pos, "microsoft.com: 1.0;");
  pos = mtp_put_u16(p, pos, 0);

  pos = mtp_put_u32(p, pos, sizeof(ops) / sizeof(ops[0]));
  for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
    pos = mtp_put_u16(p, pos, ops[i]);
  }
  pos = mtp_put_u32(p, pos, 0); // Events
  pos = mtp_put_u32(p, pos, 0); // Device Props
  pos = mtp_put_u32(p, pos, 0); // Capture Formats
  pos = mtp_put_u32(p, pos, sizeof(fmts) / sizeof(fmts[0]));
  for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
    pos = mtp_put_u16(p, pos, fmts[i]);
  }
  pos = mtp_put_str(p, pos, "EOS-OS");
  pos = mtp_put_str(p, pos, "EOS Unified Storage");
  pos = mtp_put_str(p, pos, "1.0");
  pos = mtp_put_str(p, pos, "EOS00000001");

  mtp_send_data_packet(st, MTP_OPERATION_GET_DEVICE_INFO, tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, tx_id, NULL, 0);
}

static void mtp_op_get_storage_ids(eos_usb_mtp_state_t *st, uint32_t tx_id) {
  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = 0;
  pos = mtp_put_u32(p, pos, 1);
  pos = mtp_put_u32(p, pos, EOS_USB_MTP_STORAGE_ID);
  mtp_send_data_packet(st, MTP_OPERATION_GET_STORAGE_IDS, tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, tx_id, NULL, 0);
}

static void mtp_op_get_storage_info(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  if (cmd->params[0] != EOS_USB_MTP_STORAGE_ID) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_STORAGE_ID, cmd->tx_id, NULL, 0);
    return;
  }
  uint64_t total_bytes = EOS_USB_MTP_DEFAULT_TOTAL_BYTES;
  uint64_t free_bytes = EOS_USB_MTP_DEFAULT_FREE_BYTES;
  struct statvfs sv;
  if (statvfs("/mnt/sd", &sv) == 0 && sv.f_blocks > 0) {
    total_bytes = (uint64_t)sv.f_blocks * (uint64_t)sv.f_frsize;
    free_bytes = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
  }

  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = 0;
  pos = mtp_put_u16(p, pos, EOS_USB_MTP_STORAGE_FIXED_RAM);
  pos = mtp_put_u16(p, pos, EOS_USB_MTP_FS_HIERARCHICAL);
  pos = mtp_put_u16(p, pos, 0); // Read-Write
  pos = mtp_put_u64(p, pos, total_bytes);
  pos = mtp_put_u64(p, pos, free_bytes);
  pos = mtp_put_u32(p, pos, 0xFFFFFFFF);
  pos = mtp_put_str(p, pos, EOS_USB_MTP_DEFAULT_STORAGE_NAME);
  pos = mtp_put_str(p, pos, "EOS_ROOT");

  mtp_send_data_packet(st, MTP_OPERATION_GET_STORAGE_INFO, cmd->tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_get_object_handles(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  uint32_t parent = cmd->params[2];
  size_t max_items = (EOS_USB_MTP_BUF_SIZE - EOS_USB_MTP_CONTAINER_HDR_LEN - 4) / 4;
  uint32_t *handles_ptr = (uint32_t *)(st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN + 4);
  size_t count = mtp_scan_dir(st, parent, handles_ptr, max_items);
  mtp_put_u32(st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN, 0, (uint32_t)count);
  mtp_send_data_packet(st, MTP_OPERATION_GET_OBJECT_HANDLES, cmd->tx_id,
                       st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN, 4 + count * 4);
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_get_object_info(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  eos_usb_mtp_obj_t *obj = mtp_find_by_handle(st, cmd->params[0]);
  if (!obj) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_OBJECT_HANDLE, cmd->tx_id, NULL, 0);
    return;
  }
  const char *slash = strrchr(obj->path, '/');
  const char *fname = (slash && slash[1]) ? slash + 1 : obj->path;

  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = 0;
  pos = mtp_put_u32(p, pos, EOS_USB_MTP_STORAGE_ID);
  pos = mtp_put_u16(p, pos, obj->format);
  pos = mtp_put_u16(p, pos, obj->read_only ? 1 : 0);
  pos = mtp_put_u32(p, pos, obj->size);
  pos = mtp_put_u16(p, pos, 0); // ThumbFormat
  pos = mtp_put_u32(p, pos, 0); // ThumbSize
  pos = mtp_put_u32(p, pos, 0); // ThumbW
  pos = mtp_put_u32(p, pos, 0); // ThumbH
  pos = mtp_put_u32(p, pos, 0); // ImgW
  pos = mtp_put_u32(p, pos, 0); // ImgH
  pos = mtp_put_u32(p, pos, 0); // ImgBitDepth
  pos = mtp_put_u32(p, pos, obj->parent_handle);
  pos = mtp_put_u16(p, pos, obj->is_dir ? EOS_USB_MTP_ASSOC_GENERIC_FOLDER : 0);
  pos = mtp_put_u32(p, pos, 0); // AssocDesc
  pos = mtp_put_u32(p, pos, 0); // SeqNum
  pos = mtp_put_str(p, pos, fname);
  pos = mtp_put_str(p, pos, ""); // DateCreated
  pos = mtp_put_str(p, pos, ""); // DateModified
  pos = mtp_put_str(p, pos, ""); // Keywords

  mtp_send_data_packet(st, MTP_OPERATION_GET_OBJECT_INFO, cmd->tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_get_object(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  eos_usb_mtp_obj_t *obj = mtp_find_by_handle(st, cmd->params[0]);
  if (!obj || obj->is_dir) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_OBJECT_HANDLE, cmd->tx_id, NULL, 0);
    return;
  }
  int fd = open(obj->path, O_RDONLY);
  if (fd < 0) {
    mtp_send_response(st, MTP_RESPONSE_ACCESS_DENIED, cmd->tx_id, NULL, 0);
    return;
  }
  struct stat sb;
  uint32_t file_sz = (fstat(fd, &sb) == 0) ? (uint32_t)sb.st_size : obj->size;
  uint32_t total_len = EOS_USB_MTP_CONTAINER_HDR_LEN + file_sz;

  mtp_send_container_hdr(st->tx_buf, total_len, MTP_CONTAINER_TYPE_DATA,
                         MTP_OPERATION_GET_OBJECT, cmd->tx_id);
  ssize_t n = read(fd, st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN,
                   EOS_USB_MTP_BUF_SIZE - EOS_USB_MTP_CONTAINER_HDR_LEN);
  uint32_t first_len = EOS_USB_MTP_CONTAINER_HDR_LEN + (uint32_t)((n > 0) ? n : 0);
  if (usbd_ep_start_write(st->bus_id, st->in_ep, st->tx_buf, first_len) == 0) {
    xSemaphoreTake(st->tx_sem, pdMS_TO_TICKS(EOS_USB_MTP_TX_TIMEOUT_MS));
  }

  uint32_t sent = (uint32_t)((n > 0) ? n : 0);
  while (sent < file_sz) {
    n = read(fd, st->tx_buf, EOS_USB_MTP_BUF_SIZE);
    if (n <= 0) break;
    if (usbd_ep_start_write(st->bus_id, st->in_ep, st->tx_buf, (uint32_t)n) == 0) {
      xSemaphoreTake(st->tx_sem, pdMS_TO_TICKS(EOS_USB_MTP_TX_TIMEOUT_MS));
    }
    sent += (uint32_t)n;
  }
  close(fd);

  uint16_t mps = usbd_get_ep_mps(st->bus_id, st->in_ep);
  if (mps > 0 && (total_len % mps) == 0) {
    mtp_ep_write_sync(st, NULL, 0);
  }
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_delete_object(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  eos_usb_mtp_obj_t *obj = mtp_find_by_handle(st, cmd->params[0]);
  if (!obj) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_OBJECT_HANDLE, cmd->tx_id, NULL, 0);
    return;
  }
  if (obj->read_only || mtp_is_protected_path(obj->path)) {
    mtp_send_response(st, MTP_RESPONSE_OBJECT_WRITE_PROTECTED, cmd->tx_id, NULL, 0);
    return;
  }
  int res = obj->is_dir ? rmdir(obj->path) : unlink(obj->path);
  if (res != 0) {
    mtp_send_response(st, MTP_RESPONSE_ACCESS_DENIED, cmd->tx_id, NULL, 0);
    return;
  }
  obj->in_use = false;
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_send_object_info(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  uint32_t parent_handle = cmd->params[1];
  const char *parent_path = mtp_get_parent_path(st, parent_handle);
  if (!parent_path) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_PARENT_OBJECT, cmd->tx_id, NULL, 0);
    return;
  }

  uint32_t rx_len = 0;
  if (!mtp_wait_rx_packet(st, &rx_len) || rx_len <= EOS_USB_MTP_CONTAINER_HDR_LEN + 52) {
    mtp_send_response(st, MTP_RESPONSE_INCOMPLETE_TRANSFER, cmd->tx_id, NULL, 0);
    return;
  }
  const uint8_t *p = st->rx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  uint16_t fmt = mtp_get_u16(p + 4);
  uint32_t obj_sz = mtp_get_u32(p + 8);
  char fname[EOS_USB_MTP_STR_LEN + 1];
  mtp_get_str(p + 52, rx_len - EOS_USB_MTP_CONTAINER_HDR_LEN - 52, fname, sizeof(fname));

  char full_path[EOS_USB_MTP_MAX_PATH_LEN];
  mtp_join_path(full_path, sizeof(full_path), parent_path, fname);
  if (mtp_is_protected_path(full_path)) {
    mtp_send_response(st, MTP_RESPONSE_OBJECT_WRITE_PROTECTED, cmd->tx_id, NULL, 0);
    return;
  }

  bool is_dir = (fmt == MTP_FORMAT_ASSOCIATION);
  if (is_dir) {
    if (mkdir(full_path, EOS_USB_MTP_DIR_MODE) != 0 && errno != EEXIST) {
      mtp_send_response(st, MTP_RESPONSE_ACCESS_DENIED, cmd->tx_id, NULL, 0);
      return;
    }
  }
  eos_usb_mtp_obj_t *obj = mtp_alloc_handle(st, parent_handle, full_path, is_dir, obj_sz);
  if (!obj) {
    mtp_send_response(st, MTP_RESPONSE_STORAGE_FULL, cmd->tx_id, NULL, 0);
    return;
  }
  st->pending_send_handle = obj->handle;
  st->pending_send_size = obj_sz;
  st->has_pending_send = !is_dir;

  uint32_t resp_params[3] = { EOS_USB_MTP_STORAGE_ID, parent_handle, obj->handle };
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, resp_params, 3);
}

static void mtp_op_send_object(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  eos_usb_mtp_obj_t *obj = mtp_find_by_handle(st, st->pending_send_handle);
  if (!st->has_pending_send || !obj) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_OBJECT_HANDLE, cmd->tx_id, NULL, 0);
    return;
  }
  st->has_pending_send = false;
  int fd = open(obj->path, O_CREAT | O_WRONLY | O_TRUNC, EOS_USB_MTP_FILE_MODE);
  if (fd < 0) {
    mtp_send_response(st, MTP_RESPONSE_ACCESS_DENIED, cmd->tx_id, NULL, 0);
    return;
  }

  uint32_t rx_len = 0;
  if (!mtp_wait_rx_packet(st, &rx_len) || rx_len < EOS_USB_MTP_CONTAINER_HDR_LEN) {
    close(fd);
    mtp_send_response(st, MTP_RESPONSE_INCOMPLETE_TRANSFER, cmd->tx_id, NULL, 0);
    return;
  }
  uint32_t total_container = mtp_get_u32(st->rx_buf);
  uint32_t payload_total = (total_container > EOS_USB_MTP_CONTAINER_HDR_LEN) ?
                           (total_container - EOS_USB_MTP_CONTAINER_HDR_LEN) : 0;
  uint32_t first_payload = rx_len - EOS_USB_MTP_CONTAINER_HDR_LEN;
  if (first_payload > 0) {
    write(fd, st->rx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN, first_payload);
  }
  uint32_t received = first_payload;
  while (received < payload_total) {
    if (!mtp_wait_rx_packet(st, &rx_len) || rx_len == 0) break;
    write(fd, st->rx_buf, rx_len);
    received += rx_len;
  }
  close(fd);
  obj->size = received;
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_get_object_props_supported(eos_usb_mtp_state_t *st, uint32_t tx_id) {
  static const uint16_t props[] = {
      MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
      MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_PARENT_OBJECT,
      MTP_PROPERTY_NAME
  };
  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = mtp_put_u32(p, 0, sizeof(props) / sizeof(props[0]));
  for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); i++) {
    pos = mtp_put_u16(p, pos, props[i]);
  }
  mtp_send_data_packet(st, MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED, tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, tx_id, NULL, 0);
}

static void mtp_op_get_object_prop_desc(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  uint16_t prop = (uint16_t)cmd->params[0];
  uint16_t dtype = MTP_TYPE_UINT32;
  switch (prop) {
    case MTP_PROPERTY_STORAGE_ID:
    case MTP_PROPERTY_PARENT_OBJECT:
      dtype = MTP_TYPE_UINT32;
      break;
    case MTP_PROPERTY_OBJECT_FORMAT:
    case MTP_PROPERTY_PROTECTION_STATUS:
      dtype = MTP_TYPE_UINT16;
      break;
    case MTP_PROPERTY_OBJECT_SIZE:
      dtype = MTP_TYPE_UINT64;
      break;
    case MTP_PROPERTY_OBJECT_FILE_NAME:
    case MTP_PROPERTY_NAME:
      dtype = MTP_TYPE_STR;
      break;
    default:
      mtp_send_response(st, MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED, cmd->tx_id, NULL, 0);
      return;
  }
  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = 0;
  pos = mtp_put_u16(p, pos, prop);
  pos = mtp_put_u16(p, pos, dtype);
  p[pos++] = 0; // Get-only
  if (dtype == MTP_TYPE_UINT16) {
    pos = mtp_put_u16(p, pos, 0);
  } else if (dtype == MTP_TYPE_UINT32) {
    pos = mtp_put_u32(p, pos, 0);
  } else if (dtype == MTP_TYPE_UINT64) {
    pos = mtp_put_u64(p, pos, 0);
  } else {
    p[pos++] = 0; // empty string
  }
  pos = mtp_put_u32(p, pos, 0); // GroupCode
  p[pos++] = 0; // FormFlag = None

  mtp_send_data_packet(st, MTP_OPERATION_GET_OBJECT_PROP_DESC, cmd->tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_get_object_prop_value(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  eos_usb_mtp_obj_t *obj = mtp_find_by_handle(st, cmd->params[0]);
  if (!obj) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_OBJECT_HANDLE, cmd->tx_id, NULL, 0);
    return;
  }
  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = 0;
  const char *slash = strrchr(obj->path, '/');
  const char *fname = (slash && slash[1]) ? slash + 1 : obj->path;

  switch (cmd->params[1]) {
    case MTP_PROPERTY_STORAGE_ID:
      pos = mtp_put_u32(p, pos, EOS_USB_MTP_STORAGE_ID);
      break;
    case MTP_PROPERTY_OBJECT_FORMAT:
      pos = mtp_put_u16(p, pos, obj->format);
      break;
    case MTP_PROPERTY_PROTECTION_STATUS:
      pos = mtp_put_u16(p, pos, obj->read_only ? 1 : 0);
      break;
    case MTP_PROPERTY_OBJECT_SIZE:
      pos = mtp_put_u64(p, pos, (uint64_t)obj->size);
      break;
    case MTP_PROPERTY_PARENT_OBJECT:
      pos = mtp_put_u32(p, pos, obj->parent_handle);
      break;
    case MTP_PROPERTY_OBJECT_FILE_NAME:
    case MTP_PROPERTY_NAME:
      pos = mtp_put_str(p, pos, fname);
      break;
    default:
      mtp_send_response(st, MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED, cmd->tx_id, NULL, 0);
      return;
  }
  mtp_send_data_packet(st, MTP_OPERATION_GET_OBJECT_PROP_VALUE, cmd->tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_op_get_object_prop_list(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  if (cmd->params[0] == EOS_USB_MTP_ALL_HANDLES || cmd->params[0] == EOS_USB_MTP_ROOT_HANDLE) {
    mtp_send_response(st, MTP_RESPONSE_PARAMETER_NOT_SUPPORTED, cmd->tx_id, NULL, 0);
    return;
  }
  eos_usb_mtp_obj_t *obj = mtp_find_by_handle(st, cmd->params[0]);
  if (!obj) {
    mtp_send_response(st, MTP_RESPONSE_INVALID_OBJECT_HANDLE, cmd->tx_id, NULL, 0);
    return;
  }
  const char *slash = strrchr(obj->path, '/');
  const char *fname = (slash && slash[1]) ? slash + 1 : obj->path;
  uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
  size_t pos = mtp_put_u32(p, 0, 4); // 4 elements

  // 1: StorageID
  pos = mtp_put_u32(p, pos, obj->handle);
  pos = mtp_put_u16(p, pos, MTP_PROPERTY_STORAGE_ID);
  pos = mtp_put_u16(p, pos, MTP_TYPE_UINT32);
  pos = mtp_put_u32(p, pos, EOS_USB_MTP_STORAGE_ID);
  // 2: Format
  pos = mtp_put_u32(p, pos, obj->handle);
  pos = mtp_put_u16(p, pos, MTP_PROPERTY_OBJECT_FORMAT);
  pos = mtp_put_u16(p, pos, MTP_TYPE_UINT16);
  pos = mtp_put_u16(p, pos, obj->format);
  // 3: Size
  pos = mtp_put_u32(p, pos, obj->handle);
  pos = mtp_put_u16(p, pos, MTP_PROPERTY_OBJECT_SIZE);
  pos = mtp_put_u16(p, pos, MTP_TYPE_UINT64);
  pos = mtp_put_u64(p, pos, (uint64_t)obj->size);
  // 4: FileName
  pos = mtp_put_u32(p, pos, obj->handle);
  pos = mtp_put_u16(p, pos, MTP_PROPERTY_OBJECT_FILE_NAME);
  pos = mtp_put_u16(p, pos, MTP_TYPE_STR);
  pos = mtp_put_str(p, pos, fname);

  mtp_send_data_packet(st, MTP_OPERATION_GET_OBJECT_PROP_LIST, cmd->tx_id, p, pos);
  mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
}

static void mtp_dispatch_command(eos_usb_mtp_state_t *st, const eos_usb_mtp_cmd_t *cmd) {
  switch (cmd->code) {
    case MTP_OPERATION_GET_DEVICE_INFO:
      mtp_op_get_device_info(st, cmd->tx_id);
      break;
    case MTP_OPERATION_OPEN_SESSION:
      st->session_id = cmd->params[0];
      st->session_open = true;
      mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
      break;
    case MTP_OPERATION_CLOSE_SESSION:
      st->session_open = false;
      mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
      break;
    case MTP_OPERATION_GET_STORAGE_IDS:
      mtp_op_get_storage_ids(st, cmd->tx_id);
      break;
    case MTP_OPERATION_GET_STORAGE_INFO:
      mtp_op_get_storage_info(st, cmd);
      break;
    case MTP_OPERATION_GET_NUM_OBJECTS: {
      uint32_t cnt = (uint32_t)mtp_scan_dir(st, cmd->params[2], NULL, EOS_USB_MTP_MAX_HANDLES);
      mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, &cnt, 1);
      break;
    }
    case MTP_OPERATION_GET_OBJECT_HANDLES:
      mtp_op_get_object_handles(st, cmd);
      break;
    case MTP_OPERATION_GET_OBJECT_INFO:
      mtp_op_get_object_info(st, cmd);
      break;
    case MTP_OPERATION_GET_OBJECT:
      mtp_op_get_object(st, cmd);
      break;
    case MTP_OPERATION_DELETE_OBJECT:
      mtp_op_delete_object(st, cmd);
      break;
    case MTP_OPERATION_SEND_OBJECT_INFO:
      mtp_op_send_object_info(st, cmd);
      break;
    case MTP_OPERATION_SEND_OBJECT:
      mtp_op_send_object(st, cmd);
      break;
    case MTP_OPERATION_GET_OBJECT_PROPS_SUPPORTED:
      mtp_op_get_object_props_supported(st, cmd->tx_id);
      break;
    case MTP_OPERATION_GET_OBJECT_PROP_DESC:
      mtp_op_get_object_prop_desc(st, cmd);
      break;
    case MTP_OPERATION_GET_OBJECT_PROP_VALUE:
      mtp_op_get_object_prop_value(st, cmd);
      break;
    case MTP_OPERATION_GET_OBJECT_PROP_LIST:
      mtp_op_get_object_prop_list(st, cmd);
      break;
    case MTP_OPERATION_GET_OBJECT_REFERENCES: {
      uint8_t *p = st->tx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN;
      mtp_put_u32(p, 0, 0);
      mtp_send_data_packet(st, MTP_OPERATION_GET_OBJECT_REFERENCES, cmd->tx_id, p, 4);
      mtp_send_response(st, MTP_RESPONSE_OK, cmd->tx_id, NULL, 0);
      break;
    }
    default:
      mtp_send_response(st, MTP_RESPONSE_OPERATION_NOT_SUPPORTED, cmd->tx_id, NULL, 0);
      break;
  }
}

static void mtp_worker_task(void *arg) {
  eos_usb_mtp_state_t *st = (eos_usb_mtp_state_t *)arg;
  while (st->task_running) {
    if (xSemaphoreTake(st->rx_sem, pdMS_TO_TICKS(EOS_USB_MTP_TX_TIMEOUT_MS)) != pdTRUE) {
      continue;
    }
    if (!st->configured || st->rx_nbytes < EOS_USB_MTP_CONTAINER_HDR_LEN) {
      if (st->configured) {
        usbd_ep_start_read(st->bus_id, st->out_ep, st->rx_buf, EOS_USB_MTP_BUF_SIZE);
      }
      continue;
    }
    eos_usb_mtp_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.length = mtp_get_u32(st->rx_buf);
    cmd.type = mtp_get_u16(st->rx_buf + 4);
    cmd.code = mtp_get_u16(st->rx_buf + 6);
    cmd.tx_id = mtp_get_u32(st->rx_buf + 8);
    if (cmd.type == MTP_CONTAINER_TYPE_COMMAND) {
      size_t p_bytes = (st->rx_nbytes > EOS_USB_MTP_CONTAINER_HDR_LEN) ?
                       (st->rx_nbytes - EOS_USB_MTP_CONTAINER_HDR_LEN) : 0;
      cmd.param_count = (uint8_t)(p_bytes / 4);
      if (cmd.param_count > EOS_USB_MTP_MAX_PARAMS) cmd.param_count = EOS_USB_MTP_MAX_PARAMS;
      for (uint8_t i = 0; i < cmd.param_count; i++) {
        cmd.params[i] = mtp_get_u32(st->rx_buf + EOS_USB_MTP_CONTAINER_HDR_LEN + i * 4);
      }
      mtp_dispatch_command(st, &cmd);
    }
    if (st->configured) {
      st->rx_nbytes = 0;
      usbd_ep_start_read(st->bus_id, st->out_ep, st->rx_buf, EOS_USB_MTP_BUF_SIZE);
    }
  }
  vTaskDelete(NULL);
}

// ── CherryUSB Callbacks ──────────────────────────────────────

static eos_usb_mtp_state_t *mtp_get_state(void) {
  eos_dev_t *dev = eos_dev_find_by_name("usb_mtp0");
  return (dev && dev->state) ? (eos_usb_mtp_state_t *)dev->state : NULL;
}

static void mtp_bulk_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  (void)busid;
  (void)ep;
  eos_usb_mtp_state_t *st = mtp_get_state();
  if (!st) return;
  st->rx_nbytes = nbytes;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(st->rx_sem, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void mtp_bulk_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  (void)busid;
  (void)ep;
  (void)nbytes;
  eos_usb_mtp_state_t *st = mtp_get_state();
  if (!st) return;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(st->tx_sem, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void mtp_int_cb(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  (void)busid;
  (void)ep;
  (void)nbytes;
}

static int mtp_class_interface_req(uint8_t busid, struct usb_setup_packet *setup,
                                   uint8_t **data, uint32_t *len) {
  (void)busid;
  if (!setup) return -1;
  switch (setup->bRequest) {
    case EOS_USB_MTP_CLASS_REQ_CANCEL:
    case EOS_USB_MTP_CLASS_REQ_RESET:
      return 0;
    case EOS_USB_MTP_CLASS_REQ_GET_STATUS:
      if (data && len) {
        static const uint8_t status_ok[4] = { 0x04, 0x00, 0x01, 0x20 };
        memcpy(*data, status_ok, sizeof(status_ok));
        *len = sizeof(status_ok);
      }
      return 0;
    default:
      return -1;
  }
}

static void mtp_notify_handler(uint8_t busid, uint8_t event, void *arg) {
  (void)arg;
  eos_usb_mtp_state_t *st = mtp_get_state();
  if (!st) return;
  if (event == USBD_EVENT_CONFIGURED) {
    st->configured = true;
    st->rx_nbytes = 0;
    usbd_ep_start_read(busid, st->out_ep, st->rx_buf, EOS_USB_MTP_BUF_SIZE);
  } else if (event == USBD_EVENT_RESET || event == USBD_EVENT_DISCONNECTED) {
    st->configured = false;
    st->session_open = false;
  }
}
#endif

// ── Dev Operations ───────────────────────────────────────────

bool driver_usb_mtp_init(eos_dev_t *dev) {
  if (!dev) return false;
  eos_usb_mtp_state_t *st = eos_psram_malloc_prefer(sizeof(eos_usb_mtp_state_t));
  if (!st) return false;
  memset(st, 0, sizeof(*st));
  dev->state = st;

  st->io_mutex = xSemaphoreCreateMutex();
  st->rx_sem = xSemaphoreCreateBinary();
  st->tx_sem = xSemaphoreCreateBinary();
  st->handles = eos_psram_malloc_prefer(sizeof(eos_usb_mtp_obj_t) * EOS_USB_MTP_MAX_HANDLES);
  st->rx_buf = heap_caps_aligned_alloc(EOS_USB_MTP_DMA_ALIGN, EOS_USB_MTP_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  st->tx_buf = heap_caps_aligned_alloc(EOS_USB_MTP_DMA_ALIGN, EOS_USB_MTP_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

  if (!st->io_mutex || !st->rx_sem || !st->tx_sem || !st->handles || !st->rx_buf || !st->tx_buf) {
    if (st->io_mutex) vSemaphoreDelete(st->io_mutex);
    if (st->rx_sem) vSemaphoreDelete(st->rx_sem);
    if (st->tx_sem) vSemaphoreDelete(st->tx_sem);
    if (st->handles) free(st->handles);
    if (st->rx_buf) free(st->rx_buf);
    if (st->tx_buf) free(st->tx_buf);
    free(st);
    dev->state = NULL;
    return false;
  }

  st->next_handle = 1;
  const char *path = eos_cfg_get_s(dev->cfg, "path", (char *)EOS_USB_DEFAULT_MTP_PATH);
  strncpy(st->path_spec, path, sizeof(st->path_spec) - 1);

#ifdef CONFIG_CHERRYUSB_DEVICE
  st->task_running = true;
  xTaskCreate(mtp_worker_task, "eos_mtp", EOS_USB_MTP_TASK_STACK, st,
              EOS_USB_MTP_TASK_PRIO, &st->worker_task);
#endif

  EOS_LOGI("usb_mtp: Open-source POSIX MTP responder initialized (root=%s)", st->path_spec);
  return true;
}

void driver_usb_mtp_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  eos_usb_mtp_state_t *st = (eos_usb_mtp_state_t *)dev->state;
  st->task_running = false;
  if (st->rx_sem) xSemaphoreGive(st->rx_sem);
  if (st->worker_task) {
    vTaskDelay(pdMS_TO_TICKS(EOS_USB_MTP_SHUTDOWN_WAIT_MS));
    st->worker_task = NULL;
  }
  if (st->io_mutex) vSemaphoreDelete(st->io_mutex);
  if (st->rx_sem) vSemaphoreDelete(st->rx_sem);
  if (st->tx_sem) vSemaphoreDelete(st->tx_sem);
  if (st->handles) free(st->handles);
  if (st->rx_buf) free(st->rx_buf);
  if (st->tx_buf) free(st->tx_buf);
  free(st);
  dev->state = NULL;
  EOS_LOGI("usb_mtp: shutdown complete");
}

int driver_usb_mtp_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)path;
  (void)flags;
  (void)mode;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  return EOS_DRV_FD;
}

int driver_usb_mtp_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

int driver_usb_mtp_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = ENODEV;
    return -1;
  }
  eos_usb_mtp_state_t *st = (eos_usb_mtp_state_t *)dev->state;

  switch (cmd) {
    case EOS_USB_IOCTL_GADGET_SET_MTP_PATH: {
      const char *path = va_arg(args, const char *);
      if (!path) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->io_mutex, portMAX_DELAY);
      strncpy(st->path_spec, path, sizeof(st->path_spec) - 1);
      st->path_spec[sizeof(st->path_spec) - 1] = '\0';
      st->handle_count = 0;
      st->next_handle = 1;
      xSemaphoreGive(st->io_mutex);
      EOS_LOGI("usb_mtp: set root path=%s", st->path_spec);
      return 0;
    }

    case EOS_USB_IOCTL_GADGET_GET_MTP_PATH: {
      char *out_path = va_arg(args, char *);
      if (!out_path) {
        errno = EINVAL;
        return -1;
      }
      xSemaphoreTake(st->io_mutex, portMAX_DELAY);
      strncpy(out_path, st->path_spec, EOS_USB_MTP_MAX_PATH_LEN - 1);
      out_path[EOS_USB_MTP_MAX_PATH_LEN - 1] = '\0';
      xSemaphoreGive(st->io_mutex);
      return 0;
    }

    case EOS_USB_IOCTL_MTP_ATTACH_BUS: {
#ifdef CONFIG_CHERRYUSB_DEVICE
      st->bus_id = (uint8_t)va_arg(args, uint32_t);
      struct usbd_interface *intf = va_arg(args, struct usbd_interface *);
      st->out_ep = (uint8_t)va_arg(args, uint32_t);
      st->in_ep = (uint8_t)va_arg(args, uint32_t);
      st->int_ep = (uint8_t)va_arg(args, uint32_t);

      if (intf) {
        intf->class_interface_handler = mtp_class_interface_req;
        intf->class_endpoint_handler = NULL;
        intf->vendor_handler = NULL;
        intf->notify_handler = mtp_notify_handler;
      }

      st->ep_out_desc.ep_addr = st->out_ep;
      st->ep_out_desc.ep_cb = mtp_bulk_out_cb;
      st->ep_in_desc.ep_addr = st->in_ep;
      st->ep_in_desc.ep_cb = mtp_bulk_in_cb;
      st->ep_int_desc.ep_addr = st->int_ep;
      st->ep_int_desc.ep_cb = mtp_int_cb;
      usbd_add_endpoint(st->bus_id, &st->ep_out_desc);
      usbd_add_endpoint(st->bus_id, &st->ep_in_desc);
      usbd_add_endpoint(st->bus_id, &st->ep_int_desc);
      st->bus_attached = true;
      EOS_LOGI("usb_mtp: attached to CherryUSB bus %u (out=0x%02x, in=0x%02x, int=0x%02x)",
               (unsigned int)st->bus_id, (unsigned int)st->out_ep,
               (unsigned int)st->in_ep, (unsigned int)st->int_ep);
      return 0;
#else
      errno = ENOSYS;
      return -1;
#endif
    }

    default:
      errno = ENOSYS;
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_usb_mtp = {
    EOS_DRV_INIT,
    .scope = "usb",
    .name = "mtp",
    .devname = "usb_mtp",
    .init = driver_usb_mtp_init,
    .shutdown = driver_usb_mtp_shutdown,
    .open = driver_usb_mtp_open,
    .close = driver_usb_mtp_close,
    .ioctl = driver_usb_mtp_ioctl,
};

#endif
