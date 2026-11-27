#include "test_caps.h"
#include "ebin/test_common.h"
#include "ecore/bin.h"
#include "ecore/capsmgr.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_caps_procfs(test_ctx_t *ctx) {
  FILE *f = fopen("/proc/capabilities", "r");
  report_test(ctx, "caps: fopen('/proc/capabilities') succeeds", f != NULL);
  if (!f) {
    return;
  }

  char line[EOS_BUFFER_128_LEN];
  bool has_header = false;
  bool has_flash = false;

  while (fgets(line, sizeof(line), f)) {
    if (strstr(line, "TYPE") && strstr(line, "ID") && strstr(line, "OWNER")) {
      has_header = true;
    }
    if (strstr(line, "system:flash")) {
      has_flash = true;
    }
  }
  fclose(f);

  report_test(ctx, "caps: /proc/capabilities has TYPE, ID, OWNER headers",
              has_header);
  report_test(ctx, "caps: /proc/capabilities contains system:flash", has_flash);
}

static void test_caps_api(test_ctx_t *ctx) {
  int32_t gpio_max = eos_cap_get_max_id(EOS_CAPS_GPIO);
  report_test(ctx, "caps: eos_cap_get_max_id(EOS_CAPS_GPIO) > 0",
              gpio_max > 0);

  int32_t ledc_max = eos_cap_get_max_id(EOS_CAPS_LEDC_CHANNEL);
  report_test(ctx, "caps: eos_cap_get_max_id(EOS_CAPS_LEDC_CHANNEL) > 0",
              ledc_max > 0);

  // Out of bounds tests
  bool oob_neg = eos_cap_claim(EOS_CAPS_GPIO, -1, NULL);
  report_test(ctx, "caps: negative ID claim rejected", !oob_neg);

  bool oob_high = eos_cap_claim(EOS_CAPS_GPIO, gpio_max + EOS_PERCENT_MAX, NULL);
  report_test(ctx, "caps: out-of-bounds ID claim rejected", !oob_high);

  // Find a free LEDC channel to test claim/conflict/release
  int32_t target_ch = -1;
  for (int32_t ch = 0; ch < ledc_max; ch++) {
    if (eos_cap_is_free(EOS_CAPS_LEDC_CHANNEL, ch)) {
      target_ch = ch;
      break;
    }
  }

  if (target_ch >= 0) {
    bool claim1 = eos_cap_claim_ext(EOS_CAPS_LEDC_CHANNEL, target_ch, NULL,
                                    "test_owner_1");
    report_test(ctx, "caps: claim free LEDC channel succeeds", claim1);

    report_test(ctx, "caps: channel no longer free",
                !eos_cap_is_free(EOS_CAPS_LEDC_CHANNEL, target_ch));

    char owner_buf[EOS_CAP_OWNER_MAX] = {0};
    bool got_owner = eos_cap_get_owner(EOS_CAPS_LEDC_CHANNEL, target_ch,
                                       owner_buf, sizeof(owner_buf));
    report_test(ctx, "caps: eos_cap_get_owner returns owner", got_owner);
    report_test(ctx, "caps: owner matches 'test_owner_1'",
                strcmp(owner_buf, "test_owner_1") == 0);

    // Conflict test: attempt to claim same channel with different owner
    bool claim_conflict = eos_cap_claim_ext(EOS_CAPS_LEDC_CHANNEL, target_ch,
                                            NULL, "test_owner_2");
    report_test(ctx, "caps: conflicting claim rejected", !claim_conflict);

    // Release test
    bool rel = eos_cap_release(EOS_CAPS_LEDC_CHANNEL, target_ch, NULL);
    report_test(ctx, "caps: release claimed channel succeeds", rel);
    report_test(ctx, "caps: channel is free again after release",
                eos_cap_is_free(EOS_CAPS_LEDC_CHANNEL, target_ch));
  } else {
    report_test(ctx, "caps: no free LEDC channel found for test", false);
  }

  // Device detachment cleanup test
  eos_dev_t mock_dev;
  memset(&mock_dev, 0, sizeof(mock_dev));
  strncpy(mock_dev.name, "mock_sensor", sizeof(mock_dev.name) - 1);
  mock_dev.in_use = true;

  int32_t dev_test_ch = -1;
  for (int32_t ch = ledc_max - 1; ch >= 0; ch--) {
    if (eos_cap_is_free(EOS_CAPS_LEDC_CHANNEL, ch)) {
      dev_test_ch = ch;
      break;
    }
  }

  if (dev_test_ch >= 0) {
    bool dev_claim =
        eos_cap_claim(EOS_CAPS_LEDC_CHANNEL, dev_test_ch, &mock_dev);
    report_test(ctx, "caps: claim by device succeeds", dev_claim);

    char dev_owner[EOS_CAP_OWNER_MAX] = {0};
    eos_cap_get_owner(EOS_CAPS_LEDC_CHANNEL, dev_test_ch, dev_owner,
                      sizeof(dev_owner));
    report_test(ctx, "caps: device owner formatted as dev:mock_sensor",
                strstr(dev_owner, "mock_sensor") != NULL);

    size_t released = eos_cap_release_all_dev(&mock_dev);
    report_test(ctx, "caps: eos_cap_release_all_dev releases device claims",
                released > 0);
    report_test(ctx, "caps: device resource free after release_all_dev",
                eos_cap_is_free(EOS_CAPS_LEDC_CHANNEL, dev_test_ch));
  } else {
    report_test(ctx, "caps: no free channel for mock device test", false);
  }
}

int test_caps_main(int argc, char **argv) {
  test_ctx_t ctx = {0, 0};

  printf("\n=== Running Capability Manager Unit Tests (T16) ===\n");
  fflush(stdout);

  test_caps_procfs(&ctx);
  test_caps_api(&ctx);

  printf("\nCapability Manager Tests: %d passed, %d failed\n\n", ctx.passed,
         ctx.failed);
  fflush(stdout);

  return (ctx.failed == 0) ? TEST_CAPS_SUCCESS : TEST_CAPS_FAILURE;
}

EOS_BIN_ATTR eos_bin_t test_caps_bin = {
    EOS_BIN_INITIALIZER,
    .stack_size = EOS_BIN_DEFAULT_STACK_SIZE,
    .filename = "test_caps",
    .name = "Capability Manager Test",
    .group = "Tests",
    .description = "Test capability manager claims, conflicts, and release",
    .entry_point = test_caps_main};
