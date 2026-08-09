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
//=====================================================

//=====================================================
// edriver/dispay
//=====================================================
// Those calls have to return eos_error_t
EOS_DISPLAY_IOCTL_GET_WIDTH,  // uint32_t *width
EOS_DISPLAY_IOCTL_GET_HEIGHT, // uint32_t *height

//=====================================================
// edriver/storage
//=====================================================
EOS_STORAGE_IOCTL_GET_SECTOR_SIZE, // uint32_t *sector_size
EOS_STORAGE_IOCTL_GET_CAPACITY     // uint32_t *capacity
} eos_ioctl_t;

// (^__^)==\~
