#include "ebin/test_gui_logic/test_gui_logic.h"
#include "ebin/lvgl_calc/calc_logic.h"
#include "ebin/lvgl_snake/snake_logic.h"
#include "ebin/lvgl_fm/fm_logic.h"
#include "ebin/lvgl_keypad/keypad_logic.h"
#include "ebin/lvgl_sysmon/sysmon_logic.h"
#include "ebin/lvgl_backlight/backlight_logic.h"
#include "ebin/lvgl_term/term_logic.h"
#include "ebin/lvgl_net/net_logic.h"
#include "ecore/bin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_calc_logic(test_ctx_t *ctx) {
  printf("--- Calculator Logic Tests ---\n");
  calc_engine_t eng;
  calc_logic_init(&eng);

  // 1. Initial State
  report_test(ctx, "calc: initial value is 0", calc_logic_get_display_val(&eng) == 0);
  report_test(ctx, "calc: initial state is not in error", !eng.error);

  // 2. Input digits
  calc_logic_press_token(&eng, "1");
  calc_logic_press_token(&eng, "2");
  report_test(ctx, "calc: input digits 12", calc_logic_get_display_val(&eng) == 12);

  // 3. Addition
  calc_logic_press_token(&eng, "+");
  calc_logic_press_token(&eng, "3");
  calc_logic_press_token(&eng, "4");
  calc_logic_press_token(&eng, "=");
  report_test(ctx, "calc: 12 + 34 = 46", calc_logic_get_display_val(&eng) == 46);

  // 4. Division by zero
  calc_logic_press_token(&eng, "/");
  calc_logic_press_token(&eng, "0");
  calc_logic_press_token(&eng, "=");
  report_test(ctx, "calc: division by zero triggers error", eng.error);

  // 5. Clear
  calc_logic_press_token(&eng, "C");
  report_test(ctx, "calc: clear resets error and value", !eng.error && calc_logic_get_display_val(&eng) == 0);

  // 6. Formatting
  char buf[TEST_GUI_BUF_SZ];
  eng.cur = 255;
  eng.entering = true;
  calc_logic_format_hex(&eng, buf, sizeof(buf));
  report_test(ctx, "calc: format 255 as hex (0xFF)", strstr(buf, "0xFF") != NULL);
}

static void test_snake_logic(test_ctx_t *ctx) {
  printf("--- Snake Game Logic Tests ---\n");
  snake_logic_t s;
  snake_logic_init(&s, 20, 20);

  report_test(ctx, "snake: initial length is 4", s.len == 4);
  report_test(ctx, "snake: game is alive", !s.game_over);
  report_test(ctx, "snake: score is 0", s.score == 0);

  // Test turn UP
  snake_logic_set_direction(&s, 0, -1);
  report_test(ctx, "snake: turn up queued", s.next_dy == -1);
  snake_logic_tick(&s);
  report_test(ctx, "snake: step updates dir_y", s.dir_y == -1);

  // Test 180-degree turn rejection (moving UP, cannot reverse directly DOWN)
  snake_logic_set_direction(&s, 0, 1);
  report_test(ctx, "snake: 180-degree turn rejected", s.next_dy == -1);

  // Step simulation
  for (int i = 0; i < TEST_SNAKE_STEPS; i++) {
    snake_logic_tick(&s);
  }
  report_test(ctx, "snake: steps forward without crash", !s.game_over);

  // Speed toggle
  bool fast_before = s.fast_mode;
  snake_logic_toggle_speed(&s);
  report_test(ctx, "snake: speed toggle changes fast mode flag", s.fast_mode != fast_before);
}

static void test_fm_logic(test_ctx_t *ctx) {
  printf("--- File Manager Logic Tests ---\n");
  char buf[TEST_GUI_BUF_SZ];

  fm_logic_format_size(500, buf, sizeof(buf));
  report_test(ctx, "fm: format bytes (< 1KB)", strstr(buf, "B") != NULL);

  fm_logic_format_size(2048, buf, sizeof(buf));
  report_test(ctx, "fm: format kilobytes (2.0 KB)", strstr(buf, "KB") != NULL);

  fm_logic_format_size(1048576 * 5, buf, sizeof(buf));
  report_test(ctx, "fm: format megabytes (5.0 MB)", strstr(buf, "MB") != NULL);

  fm_data_t fm;
  fm_logic_init(&fm, "/");
  report_test(ctx, "fm: init sets root cwd", strcmp(fm.cwd, "/") == 0);
  int bin_rc = fm_logic_load_dir(&fm, "/bin");
  report_test(ctx, "fm: load /bin dir succeeds", bin_rc == 0);
  report_test(ctx, "fm: /bin contains entries", kv_size(fm.entries) > 5);
  report_test(ctx, "fm: /bin cwd is /bin", strcmp(fm.cwd, "/bin") == 0);
  fm_logic_deinit(&fm);
  report_test(ctx, "fm: deinit resets vector", fm.entries.a == NULL && fm.entries.n == 0);
}

static void test_keypad_logic(test_ctx_t *ctx) {
  printf("--- Keypad Logic Tests ---\n");

  report_test(ctx, "keypad: KEY_UP found in caps", keypad_logic_find_cap_idx(KEY_UP) >= 0);
  report_test(ctx, "keypad: KEY_A found in caps", keypad_logic_find_cap_idx(KEY_A) >= 0);
  report_test(ctx, "keypad: KEY_UNKNOWN not found", keypad_logic_find_cap_idx(9999) < 0);

  report_test(ctx, "keypad: KEY_ESC is exit key", keypad_logic_is_exit_key(KEY_ESC));
  report_test(ctx, "keypad: KEY_UP is not exit key", !keypad_logic_is_exit_key(KEY_UP));

  keypad_tracker_t trk = {0};
  keypad_logic_record_press(&trk, KEY_A);
  report_test(ctx, "keypad: tracker records key press", trk.last_key == KEY_A && trk.press_count == 1);

  char buf[TEST_GUI_BUF_SZ];
  keypad_logic_format_key_press(KEY_A, buf, sizeof(buf));
  report_test(ctx, "keypad: format key press string contains key label", strstr(buf, "Key: A") != NULL);
}

static void test_sysmon_logic(test_ctx_t *ctx) {
  printf("--- System Monitor Logic Tests ---\n");
  char buf[TEST_GUI_BUF_SZ];

  sysmon_logic_format_uptime(0, buf, sizeof(buf));
  report_test(ctx, "sysmon: format 0s uptime", strstr(buf, "0s") != NULL);

  sysmon_logic_format_uptime(3665, buf, sizeof(buf));
  report_test(ctx, "sysmon: format 3665s uptime", strstr(buf, "3665s") != NULL);

  sysmon_snapshot_t snap = {0};
  sysmon_logic_take_snapshot(&snap, 0, EOS_MS_PER_SEC);
  report_test(ctx, "sysmon: total internal DRAM > 0", snap.dram.total_bytes > 0);
  report_test(ctx, "sysmon: free internal DRAM > 0", snap.dram.free_bytes > 0);
}

static void test_backlight_logic(test_ctx_t *ctx) {
  printf("--- Backlight Logic Tests ---\n");
  size_t pcount = 0;
  const int *presets = backlight_logic_get_presets(&pcount);
  report_test(ctx, "backlight: presets array populated", presets != NULL && pcount > 0);

  char buf[TEST_GUI_BUF_SZ];
  backlight_logic_format_label(100, buf, sizeof(buf));
  report_test(ctx, "backlight: format 100% label", strstr(buf, "100%") != NULL);

  backlight_logic_format_label(TEST_BACKLIGHT_SAMPLE_PCT, buf, sizeof(buf));
  report_test(ctx, "backlight: format 50% label", strstr(buf, "50%") != NULL);
}

static void test_term_logic(test_ctx_t *ctx) {
  printf("--- Terminal Logic Tests ---\n");
  char out[TEST_GUI_BUF_SZ];
  size_t out_bytes = 0;

  term_logic_sanitize_text("\033[31mRed Text\033[0m", 16, out, sizeof(out), &out_bytes);
  report_test(ctx, "term: strip ANSI color escapes", strcmp(out, "Red Text") == 0);

  term_logic_sanitize_text("Normal Text", 11, out, sizeof(out), &out_bytes);
  report_test(ctx, "term: plain text unchanged", strcmp(out, "Normal Text") == 0);

  const char *p0 = term_logic_get_preset(0);
  report_test(ctx, "term: preset 0 exists and has command", p0 != NULL && p0[0] != '\0');
}

static void test_net_logic(test_ctx_t *ctx) {
  printf("--- Network Logic Tests ---\n");

  // AP sorting test
  net_ap_item_t aps[3] = {
      {.ssid = "WeakAP", .rssi = -85, .channel = 1},
      {.ssid = "StrongAP", .rssi = -35, .channel = 6},
      {.ssid = "MidAP", .rssi = TEST_NET_RSSI_SAMPLE, .channel = 11},
  };
  net_logic_sort_aps(aps, 3);
  report_test(ctx, "net: APs sorted by RSSI descending (strongest first)",
              aps[0].rssi == -35 && aps[1].rssi == TEST_NET_RSSI_SAMPLE && aps[2].rssi == -85);

  // Toast expiry logic test
  net_logic_t net = {0};
  net_logic_set_status(&net, NET_STATUS_SUCCESS, "Test Status");
  report_test(ctx, "net: status message formatted", strcmp(net.status_msg, "Test Status") == 0);
  report_test(ctx, "net: toast expire ms is set", net.toast_expire_ms > 0);
}

int test_gui_logic_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  test_ctx_t ctx = {0};

  printf("\n=== EOS GUI Decoupled Logic Test Suite ===\n");
  test_calc_logic(&ctx);
  test_snake_logic(&ctx);
  test_fm_logic(&ctx);
  test_keypad_logic(&ctx);
  test_sysmon_logic(&ctx);
  test_backlight_logic(&ctx);
  test_term_logic(&ctx);
  test_net_logic(&ctx);

  printf("\nGUI Logic Tests: %d passed, %d failed\n", ctx.passed, ctx.failed);
  return ctx.failed == 0 ? 0 : 1;
}

EOS_BIN_ATTR const eos_bin_t test_gui_logic_app = {
    .magic = EOS_BIN_MAGIC,
    .flags = 0,
    .stack_size = 8192,
    .filename = "test_gui_logic",
    .name = "test_gui_logic",
    .group = "Diagnostics",
    .description = "Automated unit tests for decoupled GUI application business logic",
    .entry_point = test_gui_logic_main,
};
