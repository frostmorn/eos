#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
// PWM user-space control tool header
// (o_O)==\~

#define EOS_PWMCTL_DECIMAL_BASE 10
#define EOS_PWMCTL_MAX_DEV_SCAN 8
#define EOS_PWMCTL_MAX_DUTY_PCT 100

int pwmctl_main(int argc, char **argv);
