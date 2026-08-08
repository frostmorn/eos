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
EOS_BUS_IOCTL_LOCK,       // NOT IMPLEMENTED
EOS_BUS_IOCTL_UNLOCK,     // NOT IMPLEMENTED
//=====================================================

//=====================================================
// edriver/dispay
//=====================================================
// Those calls have to return eos_error_t
EOS_DISPLAY_IOCTL_GET_WIDTH,
EOS_DISPLAY_IOCTL_GET_HEIGHT,

//=====================================================
// edriver/storage
//=====================================================
EOS_STORAGE_IOCTL_GET_SECTOR_SIZE // uint32_t *sector_size
EOS_STORAGE_IOCTL_GET_CAPACITY    // uint32_t *capacity
} eos_ioctl_t;

// (^__^)==\~
