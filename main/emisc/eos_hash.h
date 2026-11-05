#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
//
// Streaming message digests and checksums with no external dependencies.
//
//   MD5, SHA-1, SHA-224, SHA-256, SHA-384, SHA-512  (RFC 1321 / FIPS 180-4)
//   CRC-32 (IEEE 802.3, as used by zlib/PNG)
//   POSIX cksum CRC (poly 0x04C11DB7, length appended)
//
// The implementation is portable C so it behaves identically on Xtensa,
// RISC-V and the host. It deliberately does not wrap mbedTLS/PSA: the crypto
// API changes between ESP-IDF releases, and these tools do not need hardware
// acceleration. The API is small enough to be re-backed by mbedTLS later.
//
// MD5 and SHA-1 are provided for checksum compatibility only. They are not
// collision resistant and must not be used for trust decisions.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  EOS_HASH_MD5 = 0,
  EOS_HASH_SHA1,
  EOS_HASH_SHA224,
  EOS_HASH_SHA256,
  EOS_HASH_SHA384,
  EOS_HASH_SHA512,
  EOS_HASH_ALG_COUNT
} eos_hash_alg_t;

#define EOS_HASH_MAX_DIGEST 64   // SHA-512
#define EOS_HASH_MAX_HEX    (EOS_HASH_MAX_DIGEST * 2 + 1)

typedef struct {
  eos_hash_alg_t alg;
  uint32_t block_size;           // 64 or 128
  uint32_t digest_size;          // bytes
  uint32_t buf_len;              // bytes pending in buf
  uint64_t total;                // bytes processed so far
  union {
    uint32_t w32[8];
    uint64_t w64[8];
  } state;
  uint8_t buf[128];
} eos_hash_ctx_t;

// Returns 0 on success, -1 for an unknown algorithm.
int eos_hash_init(eos_hash_ctx_t *ctx, eos_hash_alg_t alg);
void eos_hash_update(eos_hash_ctx_t *ctx, const void *data, size_t len);
// Writes digest_size bytes to out and wipes the context.
void eos_hash_final(eos_hash_ctx_t *ctx, uint8_t *out);

size_t eos_hash_digest_size(eos_hash_alg_t alg);          // 0 if unknown
const char *eos_hash_name(eos_hash_alg_t alg);            // "sha256", NULL if unknown

// Lowercase hex; out must hold 2*len+1 bytes.
void eos_hash_to_hex(const uint8_t *digest, size_t len, char *out);
// Parses hex (either case). Returns bytes written, or -1 on bad input / overflow.
int eos_hash_from_hex(const char *hex, size_t hex_len, uint8_t *out, size_t out_cap);
// Constant-time comparison (no early exit on the first difference).
bool eos_hash_equal(const uint8_t *a, const uint8_t *b, size_t len);

// One-shot helper.
void eos_hash_buf(eos_hash_alg_t alg, const void *data, size_t len, uint8_t *out);

// CRC-32 (IEEE). Start with crc = 0; feed the previous result back to continue.
uint32_t eos_crc32(uint32_t crc, const void *data, size_t len);

// POSIX cksum. Start with crc = 0, call update for every byte of the stream,
// then final() with the total stream length to get the value cksum prints.
uint32_t eos_cksum_update(uint32_t crc, const void *data, size_t len);
uint32_t eos_cksum_final(uint32_t crc, uint64_t total_len);
