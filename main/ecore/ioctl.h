#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

//=====================================================
// ioctl call table
// IOCTL CODE, ARGS
//=====================================================
typedef enum{
EOS_IOCTL_BASE = 100,

//=====================================================
// edriver/bus
//=====================================================
EOS_BUS_IOCTL_KID_ATTACH, // eos_dev_t *child
EOS_BUS_IOCTL_KID_DETACH, // eos_dev_t *child
EOS_BUS_IOCTL_LOCK,
EOS_BUS_IOCTL_UNLOCK,
//=====================================================

//=====================================================
// edriver/dispay
//=====================================================
EOS_DISPLAY_IOCTL_GET_WIDTH,
EOS_DISPLAY_IOCTL_GET_HEIGHT,

} eos_ioctl_t;

// (^__^)==\~
