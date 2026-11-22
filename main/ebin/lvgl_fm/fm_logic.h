#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "emisc/fancymacro.h"
#include "emisc/kvec.h"
#include "emisc/strlimits.h"

#define FM_BYTES_PER_KB       EOS_LARGE_STR_LEN
#define FM_BYTES_PER_MB       (EOS_LARGE_STR_LEN * EOS_LARGE_STR_LEN)
#define FM_BYTES_PER_GB       (EOS_LARGE_STR_LEN * EOS_LARGE_STR_LEN * EOS_LARGE_STR_LEN)

// Directory entry retaining complete POSIX stat metadata
typedef struct {
  char name[EOS_SMALL_STR_LEN];
  struct stat stat;
} fm_entry_t;

// PSRAM-backed file manager logic model
typedef struct {
  char cwd[EOS_MID_STR_LEN];
  kvec_psram_t(fm_entry_t) entries;
} fm_data_t;

// Lifecycle
int fm_logic_init(fm_data_t *d, const char *initial_path);
void fm_logic_deinit(fm_data_t *d);

// Path operations
void fm_logic_join_path(char *dst, size_t dst_len, const char *base, const char *sub);
void fm_logic_get_full_path(const fm_data_t *d, size_t index, char *buf, size_t buflen);
void fm_logic_parent_path(const char *path, char *buf, size_t buflen);

// Directory operations
int fm_logic_load_dir(fm_data_t *d, const char *path);
int fm_logic_refresh(fm_data_t *d);
int fm_logic_navigate_up(fm_data_t *d);

// Filesystem modifications
int fm_logic_delete_entry(fm_data_t *d, size_t index);
int fm_logic_create_folder(fm_data_t *d, const char *folder_name);

// Inspection & Formatting helpers
bool fm_logic_is_dir(const fm_entry_t *e);
bool fm_logic_is_exec(const fm_entry_t *e);
const char *fm_logic_get_icon(const fm_entry_t *e);
void fm_logic_format_size(off_t size, char *buf, size_t buflen);

// Sorting (parent .. first, then directories, then files, alphabetical)
void fm_logic_sort(fm_data_t *d);
