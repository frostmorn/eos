#pragma once

#include <stdbool.h>
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

#define TEST_INPUT_RETRY_INTERVAL_US 5000

typedef struct {
  const char *path;
  int max_events;
  int timeout_ms;
  bool auto_mode;
  bool forever_mode;
  const char *inject_key;
} test_input_opts_t;
