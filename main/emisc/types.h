#pragma once
#include "includes.h"

// Pin representation
typedef struct {
  char *key;
  int32_t val;
} eos_pin_t;

// Additional settings
typedef enum {
  EOS_CFG_INT,
  EOS_CFG_FLOAT,
  EOS_CFG_STR,
  EOS_CFG_PTR,
} eos_cfg_type_t;

typedef struct {
  const char *key;
  eos_cfg_type_t type;
  union {
    int32_t i;
    float f;
    char *s;
    void *ptr;
  } val;
} eos_cfg_t;

static inline int32_t eos_pin_get_no(eos_pin_t *pins, const char *key) {
  if (!pins)
    return -1;
  for (eos_pin_t *p = pins; p->key != NULL; p++)
    if (strcmp(p->key, key) == 0)
      return p->val;
  return -1; // unused in esp-idf
}

static inline eos_cfg_t *eos_cfg_get(eos_cfg_t *cfg, const char *key) {
  if (!cfg)
    return NULL;
  for (eos_cfg_t *c = cfg; c->key != NULL; c++)
    if (strcmp(c->key, key) == 0)
      return c;
  return NULL;
}

static inline int32_t eos_cfg_get_i(eos_cfg_t *cfg, const char *key,
                                    int32_t def) {
  eos_cfg_t *e = eos_cfg_get(cfg, key);
  if (!e || e->type != EOS_CFG_INT)
    return def;
  return e->val.i;
}

static inline float eos_cfg_get_f(eos_cfg_t *cfg, const char *key, float def) {
  eos_cfg_t *e = eos_cfg_get(cfg, key);
  if (!e || e->type != EOS_CFG_FLOAT)
    return def;
  return e->val.f;
}

static inline char *eos_cfg_get_s(eos_cfg_t *cfg, const char *key, char *def) {
  eos_cfg_t *e = eos_cfg_get(cfg, key);
  if (!e || e->type != EOS_CFG_STR)
    return def;
  return e->val.s;
}

static inline void *eos_cfg_get_ptr(eos_cfg_t *cfg, const char *key,
                                    void *def) {
  eos_cfg_t *e = eos_cfg_get(cfg, key);
  if (!e || e->type != EOS_CFG_PTR)
    return def;
  return e->val.ptr;
}