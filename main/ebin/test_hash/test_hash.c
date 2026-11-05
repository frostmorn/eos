#include "ecore/bin.h"
#include "emisc/eos_base64.h"
#include "emisc/eos_hash.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Forward declarations of ebin entry points
extern int md5sum_main(int argc, char **argv);
extern int sha1sum_main(int argc, char **argv);
extern int sha224sum_main(int argc, char **argv);
extern int sha256sum_main(int argc, char **argv);
extern int sha384sum_main(int argc, char **argv);
extern int sha512sum_main(int argc, char **argv);
extern int cksum_main(int argc, char **argv);
extern int base64_main(int argc, char **argv);

#define TEST_DATA_PATH "/tmp/eos_hash_test.dat"
#define TEST_LIST_PATH "/tmp/eos_hash_test.lst"

typedef struct {
  int passed;
  int failed;
} test_ctx_t;

typedef struct {
  char buf[512];
  size_t len;
} sink_t;

static void report_test(test_ctx_t *ctx, const char *name, bool ok) {
  if (ok) {
    printf("  [PASS] %s\n", name);
    ctx->passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    ctx->failed++;
  }
}

static int sink_emit(void *arg, const void *data, size_t len) {
  sink_t *s = (sink_t *)arg;
  if (s->len + len >= sizeof(s->buf)) return 1;
  memcpy(s->buf + s->len, data, len);
  s->len += len;
  s->buf[s->len] = '\0';
  return 0;
}

static bool write_file(const char *path, const void *data, size_t len) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  size_t w = fwrite(data, 1, len, f);
  fclose(f);
  return w == len;
}

static bool hex_matches(eos_hash_alg_t alg, const void *data, size_t len, const char *expected) {
  uint8_t digest[EOS_HASH_MAX_DIGEST];
  char hex[EOS_HASH_MAX_HEX];
  eos_hash_buf(alg, data, len, digest);
  eos_hash_to_hex(digest, eos_hash_digest_size(alg), hex);
  return strcmp(hex, expected) == 0;
}

static void fill_pattern(uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)((i * 7 + 3) & 0xFF);
}

// ── 1. Known-answer digests (RFC 1321, FIPS 180-4 examples) ────────────────

static void test_known_answers(test_ctx_t *ctx) {
  printf("--- digest known answers ---\n");
  const struct {
    eos_hash_alg_t alg;
    const char *input;
    const char *expected;
  } vectors[] = {
    { EOS_HASH_MD5, "", "d41d8cd98f00b204e9800998ecf8427e" },
    { EOS_HASH_MD5, "a", "0cc175b9c0f1b6a831c399e269772661" },
    { EOS_HASH_MD5, "abc", "900150983cd24fb0d6963f7d28e17f72" },
    { EOS_HASH_MD5, "message digest", "f96b697d7cb7938d525a2f31aaf161d0" },
    { EOS_HASH_MD5, "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b" },
    { EOS_HASH_SHA1, "", "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
    { EOS_HASH_SHA1, "abc", "a9993e364706816aba3e25717850c26c9cd0d89d" },
    { EOS_HASH_SHA1, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
    { EOS_HASH_SHA224, "", "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f" },
    { EOS_HASH_SHA224, "abc", "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7" },
    { EOS_HASH_SHA224, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525" },
    { EOS_HASH_SHA256, "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
    { EOS_HASH_SHA256, "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
    { EOS_HASH_SHA256, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
    { EOS_HASH_SHA384, "", "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b" },
    { EOS_HASH_SHA384, "abc", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7" },
    { EOS_HASH_SHA384, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039" },
    { EOS_HASH_SHA512, "", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
    { EOS_HASH_SHA512, "abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f" },
    { EOS_HASH_SHA512, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909" },
  };
  const size_t count = sizeof(vectors) / sizeof(vectors[0]);
  bool all = true;
  for (size_t i = 0; i < count; i++) {
    bool ok = hex_matches(vectors[i].alg, vectors[i].input, strlen(vectors[i].input), vectors[i].expected);
    if (!ok) printf("    mismatch: %s case %u\n", eos_hash_name(vectors[i].alg), (unsigned)i);
    all = all && ok;
  }
  report_test(ctx, "published digest vectors (md5, sha1, sha2 family)", all);
}

// ── 2. Padding boundaries ──────────────────────────────────────────────────

static void test_padding_boundaries(test_ctx_t *ctx) {
  printf("--- padding boundaries ---\n");
  uint8_t buf[1000];
  fill_pattern(buf, sizeof(buf));
  const struct {
    unsigned len;
    const char *sha256;
    const char *sha512;
  } rows[] = {
    { 0u, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
    { 1u, "084fed08b978af4d7d196a7446a86b58009e636b611db16211b65a9aadff29c5", "e45bf5817ddf94aa2f7a407071f0eedc6beb98f768b4cd33d1176d44d1563a45a5d7212290eb7670c6786b13591aedac86478993895e8b24e612014abaa6ba04" },
    { 55u, "e7313d333c272e639f790978283f9eb392e843d0f29b7016828bb1daa4aac70b", "14fd424b1fcadee624da946ab03f7e1def7c0d6e00f689594319881a26ff30b875ba4c622ac13100c8cc784c9c2eb23159aecbb4a02e3999062f551193e2b256" },
    { 56u, "4324d65f3c103567f5589c710bc08f8523f929a9272e3af36fc968e52abc6c27", "480fa85be41ef55a41208ca28ffc8743c91cf7d24758defe6f95bfb16de614fc86b701034896b047dd571de4318853d80e0809df162f1752cb26da6ddb94a0dd" },
    { 57u, "35df609437dcfea3279283ab79fd554e2bf78f8f7ae2de532d8ee300b09e8f73", "0582c85feba6881e7c3c371a82a645547a1a9bdb3fd0709d6a42e271697333b23d1692ff0076071fef520f46d04ca74c2b088b58a60ade3ba0c18380d230789c" },
    { 63u, "81c80242132f230c3bd41b3e63bbcff16107339549214a99614ff26664625055", "ecd42a703a4e93e163d60d55e3785b1a763838b0351bc2e6f7c94b4bfb24f9aa15da5d744ebcebe11f0fc4315d45ba3a047b6e60e07448357f2795bf34b73502" },
    { 64u, "39e3d7b6b5d075d37d053ad89b24b41bef4f3c29760c84447cab3f3be1882241", "8f3cc30b3fb5bf963688a46488249248ac2c67f0f85a145233c6c1e3c16dcd1df634c07d1d31da02576f65b9cf64e1c3fdb318b689b8a14e2e9552bcf30fb133" },
    { 65u, "aacca6ff74fdbb296d165a45cecfa04e5127bc008770fbbdd48006f2d2fae95e", "22050d1b2c5bd016c4b04c3e84f513ffbbbc057f83dc9ab196fee6684e5ee19d98b84c6538b3c5e1fc67dbc484e9d0a198254e41d0fa0cf5106991e106632419" },
    { 111u, "67d9492e628fd376e0b2efec8ca2b99b123e202cf620deb270728df979b2f73e", "68cffa6d0d76f309c9ce0d35280939f8e25990c43b7b086ccdf709be35b07d4ddba599541ff2b1c19d34ea49aeafb9659adb7ac3c0b078bb30a22d57fc6687ef" },
    { 112u, "96b928cff8528dbb99602c709a65b846cb6467acb8b722f0d758e4dc27bfc508", "d0865c524d1dddf7c23b799c413f5adcd7caefd3f66a9b49750ec81066012c25a8bcf94ddea6dc525691673097ca40e0101e897fc97218cfdb0704084e2bef4b" },
    { 113u, "cded73eed7df41eecd1ead825a38ca8763a9e910d5181c8ad8f201cc8ff2cbe4", "606314353bd419f9f7720e8297f2af8a9ed5f0bb0bed29b716bf18e34577623ae7effa46f496bd4282134e7e895284a4760a910d2ad8ec11e9dd75863058cd9c" },
    { 119u, "9ce7368e4daf32341631b492e80359dc9f594b48453cd0dd5bf0b19279cc177e", "236bdd7f38a611b5014b239245c381ae5d20a96f1e5b3178227c00056b7fa8c44ef54880085d82b7e20cd65f2bfda1326696c3f94a6a5bad0cb5ce289aa46167" },
    { 120u, "7836b787757e95e58b3ca5aec90b1b004e8deba1e50e9675af9cabf1a13a04b5", "4bd16fefaa35cc2df9fbb8ff379ec04a4070ffd5d4992574af239fa175534df87cbedceeb96be08e15090a78b83a328a2900c5411683ab47bab914591048548f" },
    { 127u, "a8d23e75d936f303d248888d9b165ee543f4cbafcad3c9dd2a79bd84faa11d07", "e0b6a20f1c0c88970a9340152cd5a1c1ecf3d3b8de55102741879438079473540133b812706e5dbec322c8c9523b6fc8c6d16ee626e87ad5fe3d2916afedc369" },
    { 128u, "d2742f1f4ac6bb7ca2b239ee18402ba8b3f9f8e652d2a72973c2b9ba11c08cf6", "99b16f17aa0b969a5b8f08f367719d516e330ccd2660b6f0688ec031dbc783de50a1cd185a2568dba75070a2403d17d4741d163578515dfd2ff756ddfe4d47b1" },
    { 129u, "307f8fc2c1622b92762e818d39a185d4d667ad49a4b07ceae1f4afa008a93ec4", "a1556e29185778aa5991e34b8884c840d589f0fbb4b8ed590e51e9ac4eb03a008125000db2671f8fe7f485b59a77b518670078ecb41a54b4cd02a7f1d2ca4c6d" },
    { 1000u, "1e9bc38cbf860b9ec31918b065f9b52476c549a782e0e7990bed8ce3868d2371", "00e36fccf193e59697a92b5ab24666ce6326d7fa16bf10832d0991ddc591112e9dfa6a636950ed9c4d67344a760654c2ff7785e1d60094d651038735b5dccabd" },
  };
  bool ok256 = true, ok512 = true;
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    ok256 = ok256 && hex_matches(EOS_HASH_SHA256, buf, rows[i].len, rows[i].sha256);
    ok512 = ok512 && hex_matches(EOS_HASH_SHA512, buf, rows[i].len, rows[i].sha512);
  }
  report_test(ctx, "sha256 at block/padding boundary lengths", ok256);
  report_test(ctx, "sha512 at block/padding boundary lengths", ok512);
  report_test(ctx, "md5 of 1000-byte pattern", hex_matches(EOS_HASH_MD5, buf, 1000, "10046f077f2082ac19676b8079f1cb1a"));
}

// ── 3. Streaming equals one-shot ───────────────────────────────────────────

static void test_streaming(test_ctx_t *ctx) {
  printf("--- streaming updates ---\n");
  uint8_t buf[1000];
  fill_pattern(buf, sizeof(buf));
  const size_t chunks[] = { 1, 7, 63, 64, 65, 127, 128, 129, 999 };
  bool all = true;
  for (int a = 0; a < EOS_HASH_ALG_COUNT; a++) {
    uint8_t whole[EOS_HASH_MAX_DIGEST], part[EOS_HASH_MAX_DIGEST];
    eos_hash_buf((eos_hash_alg_t)a, buf, sizeof(buf), whole);
    for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
      eos_hash_ctx_t h;
      eos_hash_init(&h, (eos_hash_alg_t)a);
      for (size_t off = 0; off < sizeof(buf); off += chunks[c]) {
        size_t n = sizeof(buf) - off < chunks[c] ? sizeof(buf) - off : chunks[c];
        eos_hash_update(&h, buf + off, n);
      }
      eos_hash_final(&h, part);
      if (!eos_hash_equal(whole, part, eos_hash_digest_size((eos_hash_alg_t)a))) {
        printf("    %s differs for chunk size %u\n", eos_hash_name((eos_hash_alg_t)a), (unsigned)chunks[c]);
        all = false;
      }
    }
  }
  report_test(ctx, "chunked updates match one-shot for every algorithm", all);

  eos_hash_ctx_t h;
  report_test(ctx, "init rejects unknown algorithm", eos_hash_init(&h, (eos_hash_alg_t)99) == -1);
}

// ── 4. CRCs ────────────────────────────────────────────────────────────────

static void test_crc(test_ctx_t *ctx) {
  printf("--- crc32 / cksum ---\n");
  uint8_t buf[1000];
  fill_pattern(buf, sizeof(buf));
  report_test(ctx, "crc32 check value 123456789", eos_crc32(0, "123456789", 9) == 0xCBF43926u);
  report_test(ctx, "crc32 of empty input", eos_crc32(0, "", 0) == 0u);
  report_test(ctx, "crc32 of 1000-byte pattern", eos_crc32(0, buf, sizeof(buf)) == 0x17bc2a46u);
  uint32_t split = eos_crc32(eos_crc32(0, buf, 333), buf + 333, sizeof(buf) - 333);
  report_test(ctx, "crc32 continues across calls", split == eos_crc32(0, buf, sizeof(buf)));
  report_test(ctx, "cksum 123456789 matches GNU",
              eos_cksum_final(eos_cksum_update(0, "123456789", 9), 9) == 930766865u);
  report_test(ctx, "cksum of empty input matches GNU", eos_cksum_final(0, 0) == 4294967295u);
  report_test(ctx, "cksum of 1000-byte pattern matches GNU",
              eos_cksum_final(eos_cksum_update(0, buf, sizeof(buf)), sizeof(buf)) == 3299097258u);
}

// ── 5. Hex and comparison helpers ──────────────────────────────────────────

static void test_hex_helpers(test_ctx_t *ctx) {
  printf("--- hex helpers ---\n");
  uint8_t out[4];
  char hex[9];
  const uint8_t raw[4] = { 0x00, 0xAB, 0xCD, 0xFF };
  eos_hash_to_hex(raw, 4, hex);
  report_test(ctx, "to_hex is lowercase", strcmp(hex, "00abcdff") == 0);
  report_test(ctx, "from_hex accepts upper case", eos_hash_from_hex("00ABCDFF", 8, out, sizeof(out)) == 4 && memcmp(out, raw, 4) == 0);
  report_test(ctx, "from_hex rejects odd length", eos_hash_from_hex("abc", 3, out, sizeof(out)) == -1);
  report_test(ctx, "from_hex rejects bad character", eos_hash_from_hex("zz", 2, out, sizeof(out)) == -1);
  report_test(ctx, "from_hex rejects overflow", eos_hash_from_hex("0011223344", 10, out, sizeof(out)) == -1);
  const uint8_t other[4] = { 0x00, 0xAB, 0xCD, 0xFE };
  report_test(ctx, "equal true for identical data", eos_hash_equal(raw, raw, 4));
  report_test(ctx, "equal false when last byte differs", !eos_hash_equal(raw, other, 4));
}

// ── 6. Base64 ──────────────────────────────────────────────────────────────

static bool b64_encode_is(const char *in, size_t wrap, const char *expected, bool bytewise) {
  sink_t s = { .len = 0 };
  eos_b64enc_t e;
  eos_b64enc_init(&e, wrap, sink_emit, &s);
  size_t len = strlen(in);
  if (bytewise) {
    for (size_t i = 0; i < len; i++) {
      if (eos_b64enc_update(&e, in + i, 1) != EOS_B64_OK) return false;
    }
  } else if (eos_b64enc_update(&e, in, len) != EOS_B64_OK) {
    return false;
  }
  if (eos_b64enc_final(&e) != EOS_B64_OK) return false;
  s.buf[s.len] = '\0';
  return strcmp(s.buf, expected) == 0;
}

// Returns EOS_B64_OK and fills out on success, otherwise the error code.
static int b64_decode(const char *in, bool ignore_garbage, sink_t *out) {
  out->len = 0;
  out->buf[0] = '\0';
  eos_b64dec_t d;
  eos_b64dec_init(&d, ignore_garbage, sink_emit, out);
  int rc = eos_b64dec_update(&d, in, strlen(in));
  if (rc != EOS_B64_OK) return rc;
  return eos_b64dec_final(&d);
}

static void test_base64(test_ctx_t *ctx) {
  printf("--- base64 ---\n");
  const struct {
    const char *plain;
    const char *encoded;
  } rfc[] = {
    { "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" },
    { "foob", "Zm9vYg==" }, { "fooba", "Zm9vYmE=" }, { "foobar", "Zm9vYmFy" },
  };
  bool enc_ok = true, enc_bytewise_ok = true, dec_ok = true;
  for (size_t i = 0; i < sizeof(rfc) / sizeof(rfc[0]); i++) {
    char expected_nl[32];
    snprintf(expected_nl, sizeof(expected_nl), "%s%s", rfc[i].encoded, rfc[i].encoded[0] ? "\n" : "");
    enc_ok = enc_ok && b64_encode_is(rfc[i].plain, 76, expected_nl, false);
    enc_bytewise_ok = enc_bytewise_ok && b64_encode_is(rfc[i].plain, 0, rfc[i].encoded, true);
    sink_t s;
    dec_ok = dec_ok && b64_decode(rfc[i].encoded, false, &s) == EOS_B64_OK && strcmp(s.buf, rfc[i].plain) == 0;
  }
  report_test(ctx, "RFC 4648 encode vectors (default wrap)", enc_ok);
  report_test(ctx, "encode one byte at a time, no wrap", enc_bytewise_ok);
  report_test(ctx, "RFC 4648 decode vectors", dec_ok);
  report_test(ctx, "wrap at 4 columns", b64_encode_is("foobar", 4, "Zm9v\nYmFy\n", false));
  report_test(ctx, "wrap 0 never inserts newlines",
              b64_encode_is("0123456789012345678901234567890123456789", 0,
                            "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OQ==", false));

  sink_t s;
  report_test(ctx, "decode skips newlines", b64_decode("Zm9v\nYmFy\n", false, &s) == EOS_B64_OK && strcmp(s.buf, "foobar") == 0);
  report_test(ctx, "decode rejects truncated quad", b64_decode("Zg=", false, &s) == EOS_B64_INVALID);
  report_test(ctx, "decode rejects missing padding", b64_decode("Zm9vYg", false, &s) == EOS_B64_INVALID);
  report_test(ctx, "decode rejects characters outside the alphabet", b64_decode("Zm9v!", false, &s) == EOS_B64_INVALID);
  report_test(ctx, "decode rejects data after padding", b64_decode("Zg=\nx", false, &s) == EOS_B64_INVALID);
  report_test(ctx, "decode rejects leading padding", b64_decode("=Zg=", false, &s) == EOS_B64_INVALID);
  report_test(ctx, "decode ignores garbage when asked", b64_decode("Zm9v!YmFy", true, &s) == EOS_B64_OK && strcmp(s.buf, "foobar") == 0);
  report_test(ctx, "concatenated padded streams decode", b64_decode("Zg==Zg==", false, &s) == EOS_B64_OK && strcmp(s.buf, "ff") == 0);

  // Encoder reports a failing sink
  sink_t tiny = { .len = sizeof(tiny.buf) - 2 };
  eos_b64enc_t e;
  eos_b64enc_init(&e, 0, sink_emit, &tiny);
  report_test(ctx, "emit failure is reported as EOS_B64_IO",
              eos_b64enc_update(&e, "foobar", 6) == EOS_B64_IO);
}

// ── 7. Command-line tools (exit codes; output is covered by the shell tests) ─

static void test_tools(test_ctx_t *ctx) {
  printf("--- checksum tools ---\n");
  const struct {
    const char *name;
    eos_hash_alg_t alg;
    int (*fn)(int, char **);
  } tools[] = {
    { "md5sum", EOS_HASH_MD5, md5sum_main },       { "sha1sum", EOS_HASH_SHA1, sha1sum_main },
    { "sha224sum", EOS_HASH_SHA224, sha224sum_main }, { "sha256sum", EOS_HASH_SHA256, sha256sum_main },
    { "sha384sum", EOS_HASH_SHA384, sha384sum_main }, { "sha512sum", EOS_HASH_SHA512, sha512sum_main },
  };
  const char content[] = "hello eos\n";
  unlink(TEST_DATA_PATH);
  unlink(TEST_LIST_PATH);

  bool setup = write_file(TEST_DATA_PATH, content, sizeof(content) - 1);
  report_test(ctx, "test file can be created", setup);
  if (!setup) return;

  for (size_t t = 0; t < sizeof(tools) / sizeof(tools[0]); t++) {
    char label[64];
    uint8_t digest[EOS_HASH_MAX_DIGEST];
    char hex[EOS_HASH_MAX_HEX];
    char list[EOS_HASH_MAX_HEX + 64];
    eos_hash_buf(tools[t].alg, content, sizeof(content) - 1, digest);
    eos_hash_to_hex(digest, eos_hash_digest_size(tools[t].alg), hex);

    snprintf(list, sizeof(list), "%s  %s\n", hex, TEST_DATA_PATH);
    write_file(TEST_LIST_PATH, list, strlen(list));
    char *check_argv[] = { (char *)tools[t].name, "--status", "-c", TEST_LIST_PATH, NULL };
    snprintf(label, sizeof(label), "%s -c accepts a correct list", tools[t].name);
    report_test(ctx, label, tools[t].fn(4, check_argv) == 0);

    write_file(TEST_DATA_PATH, "tampered\n", 9);
    snprintf(label, sizeof(label), "%s -c fails after the file changes", tools[t].name);
    report_test(ctx, label, tools[t].fn(4, check_argv) == 1);

    unlink(TEST_DATA_PATH);
    snprintf(label, sizeof(label), "%s -c fails when the listed file is missing", tools[t].name);
    report_test(ctx, label, tools[t].fn(4, check_argv) == 1);
    write_file(TEST_DATA_PATH, content, sizeof(content) - 1);
  }

  // Comments and blank lines are ignored; a list with no valid lines fails.
  {
    uint8_t digest[EOS_HASH_MAX_DIGEST];
    char hex[EOS_HASH_MAX_HEX];
    char list[EOS_HASH_MAX_HEX + 96];
    eos_hash_buf(EOS_HASH_SHA256, content, sizeof(content) - 1, digest);
    eos_hash_to_hex(digest, 32, hex);
    snprintf(list, sizeof(list), "# comment\n\n%s *%s\n", hex, TEST_DATA_PATH);
    write_file(TEST_LIST_PATH, list, strlen(list));
    char *argv_ok[] = { "sha256sum", "--status", "-c", TEST_LIST_PATH, NULL };
    report_test(ctx, "sha256sum -c skips comments, accepts '*' binary marker", sha256sum_main(4, argv_ok) == 0);

    const char junk[] = "this is not a checksum line\n";
    write_file(TEST_LIST_PATH, junk, sizeof(junk) - 1);
    report_test(ctx, "sha256sum -c fails when no line is valid", sha256sum_main(4, argv_ok) == 1);
  }

  char *missing_list[] = { "sha256sum", "--status", "-c", "/tmp/eos_hash_test.nolist", NULL };
  report_test(ctx, "sha256sum -c fails for a missing list file", sha256sum_main(4, missing_list) == 1);
  char *missing_file[] = { "sha256sum", "/tmp/eos_hash_test.nofile", NULL };
  report_test(ctx, "sha256sum reports an unreadable file with status 1", sha256sum_main(2, missing_file) == 1);
  char *bad_opt[] = { "sha256sum", "--no-such-option", NULL };
  report_test(ctx, "sha256sum rejects unknown options", sha256sum_main(2, bad_opt) == 1);

  char *ck_missing[] = { "cksum", "/tmp/eos_hash_test.nofile", NULL };
  report_test(ctx, "cksum reports an unreadable file with status 1", cksum_main(2, ck_missing) == 1);
  char *b64_missing[] = { "base64", "/tmp/eos_hash_test.nofile", NULL };
  report_test(ctx, "base64 reports an unreadable file with status 1", base64_main(2, b64_missing) == 1);
  char *b64_bad_wrap[] = { "base64", "-w", "abc", NULL };
  report_test(ctx, "base64 rejects a non-numeric wrap size", base64_main(3, b64_bad_wrap) == 1);

  const char bad_b64[] = "Zm9v!\n";
  write_file(TEST_LIST_PATH, bad_b64, sizeof(bad_b64) - 1);
  char *b64_dec_bad[] = { "base64", "-d", TEST_LIST_PATH, NULL };
  report_test(ctx, "base64 -d rejects invalid input with status 1", base64_main(3, b64_dec_bad) == 1);

  unlink(TEST_DATA_PATH);
  unlink(TEST_LIST_PATH);
}

int test_hash_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("\n=== Running Hash, Checksum & Base64 Unit Tests ===\n");
  test_ctx_t ctx = {0, 0};

  test_known_answers(&ctx);
  test_padding_boundaries(&ctx);
  test_streaming(&ctx);
  test_crc(&ctx);
  test_hex_helpers(&ctx);
  test_base64(&ctx);
  test_tools(&ctx);

  printf("\nHash Tests: %d passed, %d failed\n\n", ctx.passed, ctx.failed);
  return (ctx.failed == 0) ? 0 : 1;
}

EOS_BIN_ATTR eos_bin_t test_hash_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "test_hash",
    .name = "Hash Unit Tests",
    .group = "Tests",
    .description = "Unit test suite for the hash, checksum and base64 library and tools",
    .entry_point = test_hash_main,
};
