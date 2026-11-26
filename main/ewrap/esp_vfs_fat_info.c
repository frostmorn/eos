#include <esp_err.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "ecore/usb.h"

esp_err_t __real_esp_vfs_fat_info(const char *base_path, uint64_t *out_total_bytes, uint64_t *out_free_bytes);

esp_err_t __wrap_esp_vfs_fat_info(const char *base_path, uint64_t *out_total_bytes, uint64_t *out_free_bytes) {
  if (!base_path || !out_total_bytes || !out_free_bytes) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = __real_esp_vfs_fat_info(base_path, out_total_bytes, out_free_bytes);
  if (err == ESP_OK) {
    return ESP_OK;
  }
  // Check if base_path is a valid directory in EOS VFS or system root
  struct stat st;
  if (strcmp(base_path, "/") == 0 || strcmp(base_path, "/mnt/sd") == 0 ||
      (stat(base_path, &st) == 0 && S_ISDIR(st.st_mode))) {
    if (strcmp(base_path, "/mnt/sd") != 0 && __real_esp_vfs_fat_info("/mnt/sd", out_total_bytes, out_free_bytes) == ESP_OK) {
      return ESP_OK;
    }
    // Fallback capacity report for MTP directory export
    *out_total_bytes = EOS_USB_MTP_FALLBACK_TOTAL_BYTES;
    *out_free_bytes = EOS_USB_MTP_FALLBACK_FREE_BYTES;
    return ESP_OK;
  }
  return err;
}
