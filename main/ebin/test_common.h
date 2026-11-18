#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "emisc/fancymacro.h"
#include "emisc/strlimits.h"

typedef struct {
  int passed;
  int failed;
} test_ctx_t;

#define DISP_GRADIENT_TIME 3
#define TEST_DATA_PATH     "/tmp/h_test.dat"
#define TEST_LIST_PATH     "/tmp/h_test.lst"
#define CHURN_THREADS      4
#define CHURN_ITERS        25
#define TEST_PROC_STRESS_CYCLES 1000
#define TEST_BIN_DEFAULT_STACK_SIZE 16384
#define TEST_HASH_PATTERN_LEN 1000
#define TEST_HEX_STR_LEN      EOS_STR_10_LEN

static inline void report_test(test_ctx_t *ctx, const char *name, bool ok) {
  if (ok) {
    printf("  [PASS] %s\n", name);
    ctx->passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    ctx->failed++;
  }
}
