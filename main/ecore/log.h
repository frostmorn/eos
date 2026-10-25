#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
// Environment-controlled file logging & console status
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EOS_DEFAULT_LOG_FILE "/eos.log"
#define EOS_LOG_MAX_BYTES    65536U

// Initialize ESP/EOS log hook and console connection guard
void eos_log_init(void);

// Returns true if physical UART / USB-Serial-JTAG console is connected and ready
bool eos_console_is_connected(void);

// Notify logger when environment variables (EOS_LOG / EOS_LOG_FILE) are updated
void eos_log_sync_env(const char *name, const char *value);

// Query current file logging state and path
bool eos_log_is_file_enabled(const char **path_out);

#ifdef __cplusplus
}
#endif
