#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
#include <unistd.h>

// Not implemented in esp-idf
// RESEARCH: How to solve that header resolution problem?

// Retrive current working directory
// Returns 0 on success, otherwise -1
char *getcwd(char *buf, size_t size);

// Suggested alternative for EOS for determining
// current path. Since it maintains own buffer, there's no 
// need to allocate something
// Also guaranteed to always return something
char *getcwd_fast();

// Change working directory. 
// Returns 0 on success, otherwise -1
int chdir(const char*path);

