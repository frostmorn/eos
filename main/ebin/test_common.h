#pragma once

#include <stdbool.h>
#include <stdio.h>

typedef struct {
  int passed;
  int failed;
} test_ctx_t;

static inline void report_test(test_ctx_t *ctx, const char *name, bool ok) {
  if (ok) {
    printf("  [PASS] %s\n", name);
    ctx->passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    ctx->failed++;
  }
}
