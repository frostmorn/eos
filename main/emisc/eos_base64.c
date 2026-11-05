#include "emisc/eos_base64.h"

#define B64_OUT_CHUNK 192

static const char b64_alphabet[64] = {
  'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
  'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
  'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
  'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/',
};

///////////////////////////////////////////////////////////////////////
// Encoder
///////////////////////////////////////////////////////////////////////
typedef struct {
  eos_b64enc_t *e;
  char buf[B64_OUT_CHUNK];
  size_t len;
} enc_out_t;

static int enc_flush(enc_out_t *o) {
  if (o->len == 0) return 0;
  int rc = o->e->emit(o->e->arg, o->buf, o->len);
  o->len = 0;
  return rc ? EOS_B64_IO : 0;
}

static int enc_put(enc_out_t *o, char c) {
  eos_b64enc_t *e = o->e;
  if (o->len > B64_OUT_CHUNK - 3) {
    int rc = enc_flush(o);
    if (rc) return rc;
  }
  if (e->wrap && e->col == e->wrap) {
    o->buf[o->len++] = '\n';
    e->col = 0;
  }
  o->buf[o->len++] = c;
  e->col++;
  return 0;
}

static int enc_group(enc_out_t *o, const uint8_t *in, size_t n) {
  // n is 1, 2 or 3 input bytes
  uint32_t v = (uint32_t)in[0] << 16;
  if (n > 1) v |= (uint32_t)in[1] << 8;
  if (n > 2) v |= (uint32_t)in[2];
  int rc;
  if ((rc = enc_put(o, b64_alphabet[(v >> 18) & 63]))) return rc;
  if ((rc = enc_put(o, b64_alphabet[(v >> 12) & 63]))) return rc;
  if ((rc = enc_put(o, n > 1 ? b64_alphabet[(v >> 6) & 63] : '='))) return rc;
  if ((rc = enc_put(o, n > 2 ? b64_alphabet[v & 63] : '='))) return rc;
  return 0;
}

void eos_b64enc_init(eos_b64enc_t *e, size_t wrap, eos_b64_emit_fn emit, void *arg) {
  e->rem_len = 0;
  e->wrap = wrap;
  e->col = 0;
  e->emit = emit;
  e->arg = arg;
}

int eos_b64enc_update(eos_b64enc_t *e, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  enc_out_t o = { .e = e, .len = 0 };
  int rc = 0;
  while (len) {
    e->rem[e->rem_len++] = *p++;
    len--;
    if (e->rem_len == 3) {
      rc = enc_group(&o, e->rem, 3);
      e->rem_len = 0;
      if (rc) return rc;
    }
  }
  return enc_flush(&o);
}

int eos_b64enc_final(eos_b64enc_t *e) {
  enc_out_t o = { .e = e, .len = 0 };
  int rc = 0;
  if (e->rem_len) {
    rc = enc_group(&o, e->rem, e->rem_len);
    e->rem_len = 0;
    if (rc) return rc;
  }
  if (e->wrap && e->col) {
    o.buf[o.len++] = '\n';
    e->col = 0;
  }
  return enc_flush(&o);
}

///////////////////////////////////////////////////////////////////////
// Decoder
///////////////////////////////////////////////////////////////////////
static int b64_value(uint8_t c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

void eos_b64dec_init(eos_b64dec_t *d, bool ignore_garbage, eos_b64_emit_fn emit, void *arg) {
  d->acc = 0;
  d->count = 0;
  d->pad = 0;
  d->ignore_garbage = ignore_garbage;
  d->emit = emit;
  d->arg = arg;
}

int eos_b64dec_update(eos_b64dec_t *d, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint8_t out[B64_OUT_CHUNK];
  size_t o = 0;

  for (size_t i = 0; i < len; i++) {
    uint8_t c = p[i];
    if (c == '\n' || c == '\r') continue;

    int v = b64_value(c);
    if (v >= 0) {
      if (d->pad) return EOS_B64_INVALID;        // data after '='
      d->acc = (d->acc << 6) | (uint32_t)v;
      if (++d->count == 4) {
        out[o++] = (uint8_t)(d->acc >> 16);
        out[o++] = (uint8_t)(d->acc >> 8);
        out[o++] = (uint8_t)d->acc;
        d->count = 0;
        d->acc = 0;
      }
    } else if (c == '=') {
      if (d->count < 2) {
        if (d->ignore_garbage) continue;
        return EOS_B64_INVALID;
      }
      d->pad++;
      if (++d->count == 4) {
        if (d->pad == 1) {                       // three sextets + '='
          out[o++] = (uint8_t)(d->acc >> 10);
          out[o++] = (uint8_t)(d->acc >> 2);
        } else {                                  // two sextets + "=="
          out[o++] = (uint8_t)(d->acc >> 4);
        }
        d->count = 0;
        d->acc = 0;
        d->pad = 0;
      }
    } else if (!d->ignore_garbage) {
      return EOS_B64_INVALID;
    }

    if (o > B64_OUT_CHUNK - 3) {
      if (d->emit(d->arg, out, o)) return EOS_B64_IO;
      o = 0;
    }
  }
  if (o && d->emit(d->arg, out, o)) return EOS_B64_IO;
  return EOS_B64_OK;
}

int eos_b64dec_final(eos_b64dec_t *d) {
  return d->count == 0 ? EOS_B64_OK : EOS_B64_INVALID;
}
