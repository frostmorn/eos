#pragma once

#include <stdint.h>
#include <unistd.h>
#include "ecore/ioctl.h"
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

typedef struct {
  int freq;
  int duration_ms;
  int volume;
  int mult;
  int verbose;
  int is_march;
  const char *path;
} sound_opts_t;

typedef struct {
  int64_t total_render_us;
  int64_t total_write_us;
  int64_t min_write_us;
  int64_t max_write_us;
  int64_t min_render_us;
  int64_t max_render_us;
  size_t write_count;
  int64_t last_checkpoint_us;
  int64_t t0_us;
} sound_perf_stats_t;
