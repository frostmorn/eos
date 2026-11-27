#include "sysmon_logic.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

#include "emisc/fancymacro.h"

void sysmon_logic_query_mem(uint32_t caps, sysmon_mem_info_t *out) {
  if (!out) return;
  out->free_bytes = heap_caps_get_free_size(caps);
  out->total_bytes = heap_caps_get_total_size(caps);
  out->used_bytes = (out->total_bytes >= out->free_bytes)
                        ? (out->total_bytes - out->free_bytes)
                        : 0;
  out->usage_pct = (out->total_bytes > 0)
                       ? (int32_t)((out->used_bytes * (uint64_t)EOS_PERCENT_MAX) / out->total_bytes)
                       : 0;
}

void sysmon_logic_take_snapshot(sysmon_snapshot_t *snap, uint64_t start_ms, uint64_t now_ms) {
  if (!snap) return;
  sysmon_logic_query_mem(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, &snap->dram);
  sysmon_logic_query_mem(MALLOC_CAP_SPIRAM, &snap->psram);
  sysmon_logic_query_mem(MALLOC_CAP_DMA, &snap->dma);
  snap->uptime_sec = (now_ms >= start_ms) ? ((now_ms - start_ms) / (uint64_t)EOS_MS_PER_SEC) : 0;
}

void sysmon_logic_format_mem_row(const char *title, const sysmon_mem_info_t *info, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  if (!info) {
    buf[0] = '\0';
    return;
  }
  snprintf(buf, buflen, "%s: %u / %u KB (%d%%)", title ? title : "",
           (unsigned)(info->used_bytes / (size_t)EOS_LARGE_STR_LEN),
           (unsigned)(info->total_bytes / (size_t)EOS_LARGE_STR_LEN),
           (int)info->usage_pct);
}

void sysmon_logic_format_uptime(uint64_t uptime_sec, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  snprintf(buf, buflen, "EOS System Monitor   Up: %llus", (unsigned long long)uptime_sec);
}
