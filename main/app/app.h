#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

#include "misc/strlimits.h"
#include <stdint.h>

//
// App is just a file somewhere on a filesystem
//
// native app -> uses same trick as a driver for self registration
//
// binfs exposes a file for each native app which contains
// eos_native_app_manifest_t inside
//
// NOTE: theoretically we can try to implement links inside /bin (if esp-idf
// actually allows to mangle somehow a cross device linkage, it would be
// extremely cool)
//
// eos_exec("/bin/calc", "some_other_param");
// eos_exec -> opens file /bin/calc or if / isn't present seeks it in default
// directories to search [which simply our /bin, so hehehe. maybe later we add
// something else]
//
// if begining of a file equal to eos_native_app_manfiest_t.magic, it means it
// stores that specific structure
//, therefore eos_exec reads it, and then launches entry_point in it passing to
// an actual implementation
//
// otherwise we threat it as a manifest text file, which should list this
// minimal list of things
// 0. Interpreter, specifically in our scheme it seems to
// have a name Runtime -> best to use linux like "#!/usr/bin/bash"
// // Other options whose can be there or not:
// 1. AppName -> It would be used to construct menus(if not just a file name
// okay)
// 2. AppGroup -> Same intentions as previous one, but specifically for grouping
// certain utils (Other, if not in manifest)
// 1. Version
// 2. Url to check for an update package
// 3. something else user may want to insert into

#define EOS_NATIVE_APP_MAGIC 0x0C0A00AC

typedef struct {
  uint32_t magic;
  char filename[EOS_XSMALL_STR_LEN];
  char name[EOS_SMALL_STR_LEN];
  char group[EOS_XSMALL_STR_LEN];
  char description[EOS_MID_STR_LEN];
  int (*entry_point)(int argc, char **argv);
} eos_native_app_manifest_t;

#define EOS_NATIVE_APP_INIT                                                    \
  .magic = EOS_NATIVE_APP_MAGIC, .filename = "", .name = "", .group = "",      \
  .description = "", .entry_point = eos_native_app_entry_point_empty

#define EOS_NATIVE_APP_ATTR __attribute__((section(".eos_apps"))) const

// our list of apps here (^____^)==\~
extern const eos_native_app_manifest_t _eos_apps_start[];
extern const eos_native_app_manifest_t _eos_apps_end[];

// exec -> opens path
// int eos_exec(...);

// commandline with support of pipes?
int eos_system(const char *cmdline);

// Empty native app entry point implementation
int eos_native_app_entry_point_empty(int argc, char **argv);