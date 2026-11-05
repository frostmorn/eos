#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
// Dynamic ELF Loader subsystem for EOS
///////////////////////////////////////////////////////

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define EOS_ELF_MAGIC 0x464C457FU // '\x7f' 'E' 'L' 'F' (Little Endian)
#define EOS_ELF_DEFAULT_STACK_SIZE 16384U

#include "ecore/bin.h"

#define EOS_APP_MANIFEST_ATTR __attribute__((used, visibility("default"))) const eos_bin_t eos_app_manifest

/**
 * @brief Inspect an ELF binary and retrieve its embedded manifest, if present.
 *
 * @param path VFS path to the ELF file.
 * @param out_manifest Destination buffer for manifest.
 * @return true if manifest was located and valid, false otherwise.
 */
bool eos_elf_get_manifest(const char *path, eos_bin_t *out_manifest);

/**
 * @brief Test if an open file stream begins with standard ELF magic (0x7F 'E' 'L' 'F').
 *
 * @param f Open file stream positioned at offset 0. Rewinds stream before returning.
 * @return true if file is an ELF binary, false otherwise.
 */
bool eos_is_elf_stream(FILE *f);

/**
 * @brief Test if a file path points to an ELF binary.
 *
 * @param path VFS path to test.
 * @return true if file exists and starts with ELF magic, false otherwise.
 */
bool eos_is_elf_file(const char *path);

/**
 * @brief Load, relocate, and execute an ELF binary synchronously.
 *
 * Allocates executable segments in PSRAM (if supported) or SRAM,
 * resolves external symbols against the registered EOS symbol tables,
 * flushes instruction/data caches, and invokes the binary's entry point.
 * All memory is freed on completion.
 *
 * @param path VFS path to the ELF file.
 * @param argc Argument count.
 * @param argv Argument vector (argv[0] is executable path or name).
 * @return Binary exit code, or -1 on failure (with errno set).
 */
int eos_elf_exec(const char *path, int argc, char **argv);

/**
 * @brief Pipeline stage entry point adapter for executing an ELF binary in a thread.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int eos_elf_stage_entry(int argc, char **argv);

/**
 * @brief Initialize and register EOS symbol tables (POSIX, VFS, SDL) with elf_loader.
 *
 * @return 0 on success, negative error code on failure.
 */
int eos_elf_init_symbols(void);
