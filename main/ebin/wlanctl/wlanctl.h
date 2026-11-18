#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

typedef struct {
  int count;
  const char *dev;
  const char *type_filter;
  uint8_t bssid_filter[6];
  bool has_bssid;
  uint8_t src_filter[6];
  bool has_src;
  uint8_t dst_filter[6];
  bool has_dst;
  uint32_t hop_interval_ms;
  bool verbose;
  const char *pcap_path;
} wlan_dump_opts_t;
