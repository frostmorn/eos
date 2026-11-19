#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
// Display Subsystem Format & Ownership Manager (T14)
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "ecore/ioctl.h"

// Format ownership and lifecycle management
int eos_display_apply_format_pid(pid_t pid, uint32_t req_fmt);
void eos_display_release_format_pid(pid_t pid);
uint32_t eos_display_get_active_format(const char *fb_path);
pid_t eos_display_get_owner_pid(void);
