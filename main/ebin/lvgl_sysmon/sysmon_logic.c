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

static uint64_t read_proc_uptime_sec(void) {
  FILE *f = fopen("/proc/uptime", "r");
  if (!f) {
    return 0;
  }
  char line[EOS_LINE_STR_LEN];
  uint64_t sec = 0;
  if (fgets(line, sizeof(line), f)) {
    unsigned long long parsed = 0;
    if (sscanf(line, "%llu", &parsed) == 1) {
      sec = (uint64_t)parsed;
    }
  }
  fclose(f);
  return sec;
}

void sysmon_logic_read_loadavg(char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  snprintf(buf, buflen, "N/A");
  FILE *f = fopen("/proc/loadavg", "r");
  if (!f) {
    return;
  }
  char line[EOS_LINE_STR_LEN];
  if (fgets(line, sizeof(line), f)) {
    char tok[EOS_XSMALL_STR_LEN];
    if (sscanf(line, "%15s", tok) == 1) {
      snprintf(buf, buflen, "%s", tok);
    }
  }
  fclose(f);
}

void sysmon_logic_take_snapshot(sysmon_snapshot_t *snap, uint64_t start_ms, uint64_t now_ms) {
  if (!snap) return;
  sysmon_logic_query_mem(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, &snap->dram);
  sysmon_logic_query_mem(MALLOC_CAP_SPIRAM, &snap->psram);
  sysmon_logic_query_mem(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA, &snap->dma);
  uint64_t sys_up = read_proc_uptime_sec();
  snap->uptime_sec = (sys_up > 0)
                         ? sys_up
                         : ((now_ms >= start_ms) ? ((now_ms - start_ms) / (uint64_t)EOS_MS_PER_SEC) : 0);
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
