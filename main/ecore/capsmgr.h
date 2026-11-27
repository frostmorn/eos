#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
// Provides hardware resource (capabilities) management
// by preventing conflicting pin/dma/bus/controller usage
// (o_O)==\~

#include "pregen.h"
#include "ecore/dev.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define EOS_CAP_OWNER_MAX 32

#define EOS_CAP_GPIO_MAX_FALLBACK 64
#define EOS_CAP_LEDC_TIMER_MAX_FALLBACK 4
#define EOS_CAP_LEDC_CH_MAX_FALLBACK 8
#define EOS_CAP_GDMA_CH_P4_MAX 16
#define EOS_CAP_GDMA_CH_S3_MAX 5
#define EOS_CAP_SPI_MAX_FALLBACK 3
#define EOS_CAP_I2C_MAX_FALLBACK 2
#define EOS_CAP_UART_MAX_FALLBACK 3
#define EOS_CAP_RMT_MAX_FALLBACK 8
#define EOS_CAP_GPTIMER_MAX_FALLBACK 4
#define EOS_CAP_I2S_MAX_FALLBACK 2
#define EOS_CAP_INTR_MAX_FALLBACK 32

typedef enum {
  EOS_CAPS_GPIO,
  EOS_CAPS_LEDC_TIMER,
  EOS_CAPS_LEDC_CHANNEL,
  EOS_CAPS_GDMA_CHANNEL,
  EOS_CAPS_SPI,
  EOS_CAPS_I2C,
  EOS_CAPS_UART,
  EOS_CAPS_RMT,
  EOS_CAPS_GPTIMER,
  EOS_CAPS_I2S,
  EOS_CAPS_INTR,
  EOS_CAPS_COUNT
} eos_cap_type_t;

typedef struct {
  eos_cap_type_t type;
  int32_t no;
  eos_dev_t *dev;
  char owner[EOS_CAP_OWNER_MAX];
} eos_cap_t;

// Claim capability by type and index with device and optional owner string
bool eos_cap_claim_ext(eos_cap_type_t type, int32_t no, eos_dev_t *dev, const char *owner_label);

// Backward-compatible claim (calls eos_cap_claim_ext with NULL label)
bool eos_cap_claim(eos_cap_type_t type, int32_t no, eos_dev_t *dev);

// Releases specific capability
bool eos_cap_release(eos_cap_type_t type, int32_t no, eos_dev_t *dev);

// Releases all capabilities owned by dev (called on devctl detach)
size_t eos_cap_release_all_dev(eos_dev_t *dev);

// Checks if capability is free
bool eos_cap_is_free(eos_cap_type_t type, int32_t no);

// Gets owner of a capability; returns true if claimed, false if free
bool eos_cap_get_owner(eos_cap_type_t type, int32_t no, char *out_owner, size_t max_len);

// Gets maximum valid count/limit for given capability type on active SoC
int32_t eos_cap_get_max_id(eos_cap_type_t type);

// Converts capability type to string
const char *eos_cap_type_to_str(eos_cap_type_t type);

// Dumps all active capabilities into a dynamic string (caller must free with free())
char *eos_capsmgr_dump_proc(void);

// Inits capsmgr: runs static bounds checks & boot snapshot
void eos_capsmgr_init(void);
