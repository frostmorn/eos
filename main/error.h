#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

typedef enum { EOS_NO_ERROR, EOS_NO_MEM_LEFT_ERROR } eos_error_t;

extern eos_error_t eos_errno;

// Returns string representation of specified error
char *eos_error_to_str(eos_error_t err);