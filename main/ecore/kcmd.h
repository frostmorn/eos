#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EOS_KCMD_MAX_LEN 256

/**
 * @brief Store a boot command line in RTC memory with CRC32 protection.
 *        Survives esp_restart() and is consumed once on next boot.
 */
int eos_kcmd_set(const char *cmd);

/**
 * @brief Validate and consume the stored RTC boot command line (one-shot).
 * @return true if a valid command line was present and copied to out_buf.
 */
bool eos_kcmd_consume(char *out_buf, size_t out_sz);

/**
 * @brief Store an optional boot command line in RTC memory and reboot.
 */
void eos_reboot_with_kcmd(const char *cmd);
