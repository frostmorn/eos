#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
//
// Streaming Base64 (RFC 4648, standard alphabet) encoder and decoder.
// Output is delivered through an emit callback so callers never need to size
// an output buffer, and input can arrive in arbitrary chunks.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Receives output bytes. Return 0 to continue, non-zero to abort.
typedef int (*eos_b64_emit_fn)(void *arg, const void *data, size_t len);

#define EOS_B64_OK        0
#define EOS_B64_INVALID  (-1)   // malformed input while decoding
#define EOS_B64_IO       (-2)   // emit callback reported a failure

typedef struct {
  uint8_t rem[3];
  uint8_t rem_len;
  size_t wrap;                  // 0 = never insert newlines
  size_t col;                   // characters written on the current line
  eos_b64_emit_fn emit;
  void *arg;
} eos_b64enc_t;

void eos_b64enc_init(eos_b64enc_t *e, size_t wrap, eos_b64_emit_fn emit, void *arg);
int eos_b64enc_update(eos_b64enc_t *e, const void *data, size_t len);
// Flushes padding and, when wrapping is enabled, the final newline.
int eos_b64enc_final(eos_b64enc_t *e);

typedef struct {
  uint32_t acc;
  uint8_t count;                // sextets collected in the current quad (including '=')
  uint8_t pad;                  // '=' characters seen in the current quad
  bool ignore_garbage;          // skip characters outside the alphabet
  eos_b64_emit_fn emit;
  void *arg;
} eos_b64dec_t;

void eos_b64dec_init(eos_b64dec_t *d, bool ignore_garbage, eos_b64_emit_fn emit, void *arg);
// CR and LF are always skipped.
int eos_b64dec_update(eos_b64dec_t *d, const void *data, size_t len);
// Returns EOS_B64_INVALID if the input ended in the middle of a quad.
int eos_b64dec_final(eos_b64dec_t *d);
