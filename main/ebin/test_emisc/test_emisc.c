#include "ecore/bin.h"
#include "emisc/fancymacro.h"
#include "emisc/fancytree.h"
#include "emisc/fnv-1a-32.h"
#include "emisc/kvec.h"
#include "emisc/netmisc.h"
#include "emisc/strlimits.h"
#include "emisc/types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── Test Context ──────────────────────────────────────────────

typedef struct {
  int passed;
  int failed;
} test_ctx_t;

static void report_test(test_ctx_t *ctx, const char *name, bool ok) {
  if (ok) {
    printf("  [PASS] %s\n", name);
    ctx->passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    ctx->failed++;
  }
}

// ── 1. FNV-1a 32-bit Hash Tests ───────────────────────────────

static void test_fnv1a(test_ctx_t *ctx) {
  printf("--- FNV-1a Hash Tests ---\n");

  // Initial hash value on empty string
  uint32_t h_empty = fnv_32a_cstr("", FNV_1A_INITIAL_HVAL);
  report_test(ctx, "fnv1a: empty string matches FNV_1A_INITIAL_HVAL",
              h_empty == FNV_1A_INITIAL_HVAL);

  uint32_t h_buf_empty = fnv_32a_buf("", 0, FNV_1A_INITIAL_HVAL);
  report_test(ctx, "fnv1a: empty buffer matches FNV_1A_INITIAL_HVAL",
              h_buf_empty == FNV_1A_INITIAL_HVAL);

  // Known FNV-1a 32-bit test vectors
  // "a" -> 0xe40c292c
  uint32_t h_a = fnv_32a_cstr("a", FNV_1A_INITIAL_HVAL);
  report_test(ctx, "fnv1a: vector \"a\" (0xe40c292c)", h_a == 0xe40c292cU);

  // "foobar" -> 0xbf9cf968
  uint32_t h_foobar = fnv_32a_cstr("foobar", FNV_1A_INITIAL_HVAL);
  report_test(ctx, "fnv1a: vector \"foobar\" (0xbf9cf968)", h_foobar == 0xbf9cf968U);

  // Buffer and cstr consistency
  const char *sample = "EOS Unified Device Model";
  uint32_t h_str = fnv_32a_cstr(sample, FNV_1A_INITIAL_HVAL);
  uint32_t h_buf = fnv_32a_buf(sample, strlen(sample), FNV_1A_INITIAL_HVAL);
  report_test(ctx, "fnv1a: cstr and buf produce identical hash", h_str == h_buf);

  // Incremental hashing: hash("foo") followed by hash("bar") equals hash("foobar")
  uint32_t h_inc = fnv_32a_cstr("foo", FNV_1A_INITIAL_HVAL);
  h_inc = fnv_32a_cstr("bar", h_inc);
  report_test(ctx, "fnv1a: incremental stream hashing", h_inc == h_foobar);

  // Collision check on distinct strings
  uint32_t h_dev0 = fnv_32a_cstr("wifi0", FNV_1A_INITIAL_HVAL);
  uint32_t h_dev1 = fnv_32a_cstr("sta0", FNV_1A_INITIAL_HVAL);
  report_test(ctx, "fnv1a: distinct inputs produce distinct hashes", h_dev0 != h_dev1);
}

// ── 2. kvec Vector & Roundup Tests ────────────────────────────

static void test_kvec(test_ctx_t *ctx) {
  printf("--- kvec Dynamic Array Tests ---\n");

  // kv_roundup32 power-of-2 rounding
  uint32_t r0 = 0;
  kv_roundup32(r0);
  report_test(ctx, "kvec: kv_roundup32(0) == 0", r0 == 0);

  uint32_t r1 = 1;
  kv_roundup32(r1);
  report_test(ctx, "kvec: kv_roundup32(1) == 1", r1 == 1);

  uint32_t r3 = 3;
  kv_roundup32(r3);
  report_test(ctx, "kvec: kv_roundup32(3) == 4", r3 == 4);

  uint32_t r30 = 30;
  kv_roundup32(r30);
  report_test(ctx, "kvec: kv_roundup32(30) == 32", r30 == 32);

  uint32_t r1000 = 1000;
  kv_roundup32(r1000);
  report_test(ctx, "kvec: kv_roundup32(1000) == 1024", r1000 == 1024);

  // kvec_t operations
  kvec_t(int) v;
  kv_init(v);
  report_test(ctx, "kvec: kv_init size is 0", kv_size(v) == 0 && kv_max(v) == 0);

  // Push elements
  for (int i = 0; i < 16; i++) {
    kv_push(int, v, i * 5);
  }
  report_test(ctx, "kvec: kv_push 16 elements size check", kv_size(v) == 16);
  report_test(ctx, "kvec: kv_A element retrieval", kv_A(v, 0) == 0 && kv_A(v, 3) == 15 && kv_A(v, 15) == 75);

  // Pop element
  int popped = kv_pop(v);
  report_test(ctx, "kvec: kv_pop returns last element and shrinks size", popped == 75 && kv_size(v) == 15);

  // Push additional element with kv_push
  kv_push(int, v, 4242);
  report_test(ctx, "kvec: kv_push element and size check",
              kv_size(v) == 16 && kv_A(v, 15) == 4242);

  // Resize vector capacity
  kv_resize(int, v, 32);
  report_test(ctx, "kvec: kv_resize expands capacity", kv_max(v) == 32);

  // kv_drop_fast
  kv_drop_fast(int, v, 0);
  report_test(ctx, "kvec: kv_drop_fast removes element and decrements size", kv_size(v) == 15);

  // Vector copy
  kvec_t(int) v_copy;
  kv_init(v_copy);
  kv_copy(int, v_copy, v);
  report_test(ctx, "kvec: kv_copy duplicates size and elements",
              kv_size(v_copy) == kv_size(v) && kv_A(v_copy, 0) == 4242 && kv_A(v_copy, 3) == 15);

  kv_destroy(v);
  kv_destroy(v_copy);
}

// ── 3. fancytree Generic Hierarchy Tests ───────────────────────

typedef struct emisc_test_node_s emisc_test_node_t;
struct emisc_test_node_s {
  EOS_TREE_FIELDS(emisc_test_node_t);
  int id;
};

EOS_TREE_DECLARE(emisc_test_node_t);
EOS_TREE_DEFINE(emisc_test_node_t)

static bool walk_visitor(emisc_test_node_t *node, void *ctx) {
  int *visit_count = (int *)ctx;
  if (node) {
    (*visit_count)++;
  }
  return true;
}

static bool walk_stop_early(emisc_test_node_t *node, void *ctx) {
  int *visit_count = (int *)ctx;
  (*visit_count)++;
  return (node->id != 1); // stop when id == 1
}

static bool find_by_id(emisc_test_node_t *node, void *ctx) {
  int target = *(int *)ctx;
  return (node && node->id == target);
}

static void detach_callback(emisc_test_node_t *node, void *ctx) {
  int *callback_count = (int *)ctx;
  if (node) {
    (*callback_count)++;
  }
}

static void test_fancytree(test_ctx_t *ctx) {
  printf("--- fancytree N-ary Tree Tests ---\n");

  emisc_test_node_t root = {.id = 0};
  emisc_test_node_t c1 = {.id = 1};
  emisc_test_node_t c2 = {.id = 2};
  emisc_test_node_t g1 = {.id = 3};

  // Attach children
  bool ok_c1 = emisc_test_node_t_tree_attach(&c1, &root);
  bool ok_c2 = emisc_test_node_t_tree_attach(&c2, &root);
  bool ok_g1 = emisc_test_node_t_tree_attach(&g1, &c1);
  report_test(ctx, "fancytree: attach children and grandchildren",
              ok_c1 && ok_c2 && ok_g1 && root.child == &c1 && c1.next == &c2 && c1.child == &g1);

  // Reject self attachment and duplicate attachment
  bool self_attach = emisc_test_node_t_tree_attach(&root, &root);
  bool dup_attach = emisc_test_node_t_tree_attach(&c1, &root);
  report_test(ctx, "fancytree: reject self-attach and duplicate attach",
              !self_attach && !dup_attach);

  // Cycle prevention: cannot attach root to its descendant g1
  bool cycle_attach = emisc_test_node_t_tree_attach(&root, &g1);
  report_test(ctx, "fancytree: cycle prevention (attach parent to descendant)", !cycle_attach);

  // Tree walk
  int count_walked = 0;
  emisc_test_node_t_tree_walk(&root, walk_visitor, &count_walked);
  report_test(ctx, "fancytree: pre-order walk visits all 4 nodes", count_walked == 4);

  // Early stop walk
  int count_early = 0;
  bool walk_res = emisc_test_node_t_tree_walk(&root, walk_stop_early, &count_early);
  report_test(ctx, "fancytree: walk stops early on false return", !walk_res && count_early < 4);

  // Find child
  int target = 2;
  emisc_test_node_t *found = emisc_test_node_t_tree_find_child(&root, find_by_id, &target);
  report_test(ctx, "fancytree: find_child locates existing child", found == &c2);

  int not_found_id = 99;
  emisc_test_node_t *not_found = emisc_test_node_t_tree_find_child(&root, find_by_id, &not_found_id);
  report_test(ctx, "fancytree: find_child returns NULL for absent child", not_found == NULL);

  // Detach single node and verify child reparenting
  emisc_test_node_t_tree_detach(&c1);
  report_test(ctx, "fancytree: detach reparents orphan g1 to root",
              c1.parent == NULL && g1.parent == &root);

  // Detach subtree with callback
  int detached_count = 0;
  emisc_test_node_t_tree_detach_subtree(&root, detach_callback, &detached_count);
  report_test(ctx, "fancytree: detach_subtree visits remaining nodes", detached_count == 3);
}

// ── 4. netmisc Protocol & Math Tests ──────────────────────────

static void test_netmisc(test_ctx_t *ctx) {
  printf("--- netmisc IP, Mask, MAC & Protocol Math Tests ---\n");

  // IPv4 to u32
  uint32_t ip1 = eos_net_ipv4_to_u32("192.168.1.1");
  report_test(ctx, "netmisc: 192.168.1.1 -> 0xC0A80101", ip1 == 0xC0A80101U);

  uint32_t ip2 = eos_net_ipv4_to_u32("10.0.0.254");
  report_test(ctx, "netmisc: 10.0.0.254 -> 0x0A0000FE", ip2 == 0x0A0000FEU);

  report_test(ctx, "netmisc: 255.255.255.255 -> 0xFFFFFFFF",
              eos_net_ipv4_to_u32("255.255.255.255") == 0xFFFFFFFFU);
  report_test(ctx, "netmisc: 0.0.0.0 -> 0x00000000", eos_net_ipv4_to_u32("0.0.0.0") == 0);
  report_test(ctx, "netmisc: NULL / empty -> 0", eos_net_ipv4_to_u32(NULL) == 0 && eos_net_ipv4_to_u32("") == 0);

  // u32 to IPv4 formatting
  char ip_str[20];
  eos_net_u32_to_ipv4(0xC0A80101U, ip_str, sizeof(ip_str));
  report_test(ctx, "netmisc: 0xC0A80101 -> \"192.168.1.1\"", strcmp(ip_str, "192.168.1.1") == 0);

  // Prefix to u32
  report_test(ctx, "netmisc: prefix 24 -> 0xFFFFFF00", eos_net_prefix_to_u32(24) == 0xFFFFFF00U);
  report_test(ctx, "netmisc: prefix 16 -> 0xFFFF0000", eos_net_prefix_to_u32(16) == 0xFFFF0000U);
  report_test(ctx, "netmisc: prefix 8 -> 0xFF000000", eos_net_prefix_to_u32(8) == 0xFF000000U);
  report_test(ctx, "netmisc: prefix 32 -> 0xFFFFFFFF", eos_net_prefix_to_u32(32) == 0xFFFFFFFFU);
  report_test(ctx, "netmisc: prefix 0 -> 0", eos_net_prefix_to_u32(0) == 0);

  // u32 to prefix
  report_test(ctx, "netmisc: 0xFFFFFF00 -> prefix 24", eos_net_u32_to_prefix(0xFFFFFF00U) == 24);
  report_test(ctx, "netmisc: 0xFFFF0000 -> prefix 16", eos_net_u32_to_prefix(0xFFFF0000U) == 16);

  // Mask string to prefix
  report_test(ctx, "netmisc: mask \"255.255.255.0\" -> 24",
              eos_net_mask_to_prefix("255.255.255.0") == 24);
  report_test(ctx, "netmisc: mask \"255.0.0.0\" -> 8", eos_net_mask_to_prefix("255.0.0.0") == 8);
  report_test(ctx, "netmisc: mask \"16\" numeric string -> 16", eos_net_mask_to_prefix("16") == 16);

  // Prefix to mask string
  char mask_buf[20];
  eos_net_prefix_to_mask(24, mask_buf, sizeof(mask_buf));
  report_test(ctx, "netmisc: prefix 24 -> \"255.255.255.0\"", strcmp(mask_buf, "255.255.255.0") == 0);

  // Broadcast calculation
  char bcast[20];
  eos_net_calc_broadcast("192.168.1.100", "255.255.255.0", bcast, sizeof(bcast));
  report_test(ctx, "netmisc: calc_broadcast 192.168.1.100/24 -> 192.168.1.255",
              strcmp(bcast, "192.168.1.255") == 0);

  eos_net_calc_broadcast("10.5.2.1", "255.255.0.0", bcast, sizeof(bcast));
  report_test(ctx, "netmisc: calc_broadcast 10.5.2.1/16 -> 10.5.255.255",
              strcmp(bcast, "10.5.255.255") == 0);

  // Default gateway calculation
  char gw[20];
  eos_net_calc_default_gw("192.168.50.88", gw, sizeof(gw));
  report_test(ctx, "netmisc: calc_default_gw 192.168.50.88 -> 192.168.50.1",
              strcmp(gw, "192.168.50.1") == 0);

  // CIDR notation parsing
  char cidr_ip[20], cidr_mask[20];
  int cidr_prefix = 0;
  bool cidr_ok = eos_net_parse_cidr("192.168.4.1/24", cidr_ip, sizeof(cidr_ip),
                                    cidr_mask, sizeof(cidr_mask), &cidr_prefix);
  report_test(ctx, "netmisc: parse_cidr \"192.168.4.1/24\"",
              cidr_ok && strcmp(cidr_ip, "192.168.4.1") == 0 &&
              strcmp(cidr_mask, "255.255.255.0") == 0 && cidr_prefix == 24);

  // MAC parsing & formatting
  uint8_t mac[6] = {0};
  bool mac_colons = eos_net_parse_mac("12:34:56:78:9a:bc", mac);
  report_test(ctx, "netmisc: parse_mac with colons",
              mac_colons && mac[0] == 0x12 && mac[1] == 0x34 && mac[2] == 0x56 &&
              mac[3] == 0x78 && mac[4] == 0x9a && mac[5] == 0xbc);

  uint8_t mac_hyphens[6] = {0};
  bool mac_hyph_ok = eos_net_parse_mac("de-ad-be-ef-00-01", mac_hyphens);
  report_test(ctx, "netmisc: parse_mac with hyphens",
              mac_hyph_ok && mac_hyphens[0] == 0xde && mac_hyphens[3] == 0xef && mac_hyphens[5] == 0x01);

  char mac_fmt[20];
  eos_net_format_mac(mac, mac_fmt, sizeof(mac_fmt));
  report_test(ctx, "netmisc: format_mac", strcmp(mac_fmt, "12:34:56:78:9a:bc") == 0);

  report_test(ctx, "netmisc: parse_mac invalid string returns false", !eos_net_parse_mac("not-a-mac", mac));

  // Hex <-> Bytes conversions
  uint8_t byte_buf[16] = {0};
  ssize_t converted = eos_net_hex_to_bytes("01020304a1b2c3d4", byte_buf, sizeof(byte_buf));
  report_test(ctx, "netmisc: hex_to_bytes length check", converted == 8);
  report_test(ctx, "netmisc: hex_to_bytes content check",
              byte_buf[0] == 0x01 && byte_buf[3] == 0x04 && byte_buf[4] == 0xa1 && byte_buf[7] == 0xd4);

  char hex_out[32] = {0};
  size_t hex_len = eos_net_bytes_to_hex(byte_buf, 8, hex_out, sizeof(hex_out));
  report_test(ctx, "netmisc: bytes_to_hex round-trip",
              hex_len == 16 && strcmp(hex_out, "01020304a1b2c3d4") == 0);

  report_test(ctx, "netmisc: hex_to_bytes rejects odd hex length",
              eos_net_hex_to_bytes("abc", byte_buf, sizeof(byte_buf)) == -1);
  report_test(ctx, "netmisc: hex_to_bytes rejects invalid hex chars",
              eos_net_hex_to_bytes("zzff", byte_buf, sizeof(byte_buf)) == -1);

  // WiFi auth name & RSSI quality
  report_test(ctx, "netmisc: wifi_auth_name OPEN", strcmp(eos_net_wifi_auth_name(0), "OPEN") == 0);
  report_test(ctx, "netmisc: wifi_auth_name WPA2_PSK", strcmp(eos_net_wifi_auth_name(3), "WPA2_PSK") == 0);
  report_test(ctx, "netmisc: wifi_auth_name out of bounds -> UNKNOWN",
              strcmp(eos_net_wifi_auth_name(50), "UNKNOWN") == 0);

  report_test(ctx, "netmisc: rssi_to_quality -50 dBm -> 100%", eos_net_rssi_to_quality(-50) == 100);
  report_test(ctx, "netmisc: rssi_to_quality -100 dBm -> 0%", eos_net_rssi_to_quality(-100) == 0);
  report_test(ctx, "netmisc: rssi_to_quality -75 dBm -> 50%", eos_net_rssi_to_quality(-75) == 50);

  // PCAP header builders
  uint8_t pcap_global[32] = {0};
  size_t g_len = eos_net_pcap_write_global_header(pcap_global, sizeof(pcap_global));
  eos_pcap_hdr_t *ghdr = (eos_pcap_hdr_t *)pcap_global;
  report_test(ctx, "netmisc: pcap_write_global_header magic & snaplen",
              g_len == sizeof(eos_pcap_hdr_t) && ghdr->magic_number == 0xa1b2c3d4 && ghdr->network == 80);

  uint8_t pcap_pkt[32] = {0};
  size_t p_len = eos_net_pcap_write_packet_header(pcap_pkt, sizeof(pcap_pkt), 100, 200, 64, 64);
  eos_pcap_packet_hdr_t *phdr = (eos_pcap_packet_hdr_t *)pcap_pkt;
  report_test(ctx, "netmisc: pcap_write_packet_header fields",
              p_len == sizeof(eos_pcap_packet_hdr_t) && phdr->ts_sec == 100 &&
              phdr->ts_usec == 200 && phdr->incl_len == 64);

  // Packet matching
  uint8_t m_filter[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  uint8_t m_match[6]  = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  uint8_t m_diff[6]   = {0x00, 0x11, 0x22, 0x33, 0x44, 0x56};
  report_test(ctx, "netmisc: wifi_match_mac true check", eos_net_wifi_match_mac(m_match, m_filter));
  report_test(ctx, "netmisc: wifi_match_mac false check", !eos_net_wifi_match_mac(m_diff, m_filter));

  report_test(ctx, "netmisc: wifi_match_type exact match", eos_net_wifi_match_type("MGMT", "mgmt"));
  report_test(ctx, "netmisc: wifi_match_type wildcard \"all\"", eos_net_wifi_match_type("DATA", "all"));
  report_test(ctx, "netmisc: wifi_match_type mismatch", !eos_net_wifi_match_type("CTRL", "mgmt"));
}

// ── 5. types.h Pin and Configuration Tests ────────────────────

static void test_types(test_ctx_t *ctx) {
  printf("--- types Pin & Config Structure Tests ---\n");

  eos_pin_t pins[] = {
      {"cs", 5},
      {"dc", 4},
      {"rst", 48},
      {"sck", 12},
      {NULL, -1},
  };

  report_test(ctx, "types: eos_pin_get_no finds existing pin", eos_pin_get_no(pins, "dc") == 4);
  report_test(ctx, "types: eos_pin_get_no returns -1 for absent pin", eos_pin_get_no(pins, "miso") == -1);
  report_test(ctx, "types: eos_pin_get_no handles NULL pins safely", eos_pin_get_no(NULL, "cs") == -1);

  eos_cfg_t configs[] = {
      {"baudrate", EOS_CFG_INT, {.i = 115200}},
      {"gamma", EOS_CFG_FLOAT, {.f = 2.2f}},
      {"hostname", EOS_CFG_STR, {.s = "eos-testbed"}},
      {"handle", EOS_CFG_PTR, {.ptr = (void *)0x1234}},
      {NULL, EOS_CFG_INT, {.i = 0}},
  };

  report_test(ctx, "types: eos_cfg_get_i retrieves int", eos_cfg_get_i(configs, "baudrate", 9600) == 115200);
  report_test(ctx, "types: eos_cfg_get_i returns default on missing key", eos_cfg_get_i(configs, "missing", 9600) == 9600);
  report_test(ctx, "types: eos_cfg_get_f retrieves float", eos_cfg_get_f(configs, "gamma", 1.0f) == 2.2f);
  report_test(ctx, "types: eos_cfg_get_s retrieves string", strcmp(eos_cfg_get_s(configs, "hostname", "none"), "eos-testbed") == 0);
  report_test(ctx, "types: eos_cfg_get_ptr retrieves pointer", eos_cfg_get_ptr(configs, "handle", NULL) == (void *)0x1234);
}

// ── 6. fancymacro Helper Tests ────────────────────────────────

static void test_fancymacro(test_ctx_t *ctx) {
  printf("--- fancymacro Macro Tests ---\n");

  int sample_arr[7] = {1, 2, 3, 4, 5, 6, 7};
  report_test(ctx, "fancymacro: EOS_ARR_COUNT counts 7 elements", EOS_ARR_COUNT(sample_arr) == 7);

  const char *str_42 = EOS_STR(42);
  report_test(ctx, "fancymacro: EOS_STR stringification", strcmp(str_42, "42") == 0);
}

// ── Main Entry Point ──────────────────────────────────────────

int test_emisc_main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("\n=== Running emisc Math & Utility Test Suite ===\n\n");

  test_ctx_t ctx = {.passed = 0, .failed = 0};

  test_fnv1a(&ctx);
  printf("\n");
  test_kvec(&ctx);
  printf("\n");
  test_fancytree(&ctx);
  printf("\n");
  test_netmisc(&ctx);
  printf("\n");
  test_types(&ctx);
  printf("\n");
  test_fancymacro(&ctx);

  printf("\n==============================================\n");
  printf("emisc Tests: %d passed, %d failed.\n\n", ctx.passed, ctx.failed);

  return (ctx.failed == 0) ? 0 : 1;
}

EOS_BIN_ATTR const eos_bin_t test_emisc_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "test_emisc",
    .name = "emisc Unit Tests",
    .group = "Tests",
    .description = "Unit tests for emisc math, tree, vector, hash, and network utilities",
    .entry_point = test_emisc_main,
};
