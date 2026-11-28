#pragma once

#include <stddef.h>
#include <stdint.h>

#include "emisc/strlimits.h"

typedef struct {
  size_t total_bytes;
  size_t free_bytes;
  size_t used_bytes;
  int32_t usage_pct;
} sysmon_mem_info_t;

typedef struct {
  sysmon_mem_info_t dram;
  sysmon_mem_info_t psram;
  sysmon_mem_info_t dma;
  uint64_t uptime_sec;
} sysmon_snapshot_t;

void sysmon_logic_query_mem(uint32_t caps, sysmon_mem_info_t *out);
void sysmon_logic_take_snapshot(sysmon_snapshot_t *snap, uint64_t start_ms, uint64_t now_ms);
void sysmon_logic_format_mem_row(const char *title, const sysmon_mem_info_t *info, char *buf, size_t buflen);
void sysmon_logic_format_uptime(uint64_t uptime_sec, char *buf, size_t buflen);
void sysmon_logic_read_loadavg(char *buf, size_t buflen);
