#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////

// TODO: elliminate that
#include "includes.h"

#include <esp_vfs.h>
#include <dirent.h>

///////////////////////////////////////////////////////
// Structure representing a device driver for EOS
///////////////////////////////////////////////////////
typedef struct eos_dev_t eos_dev_t;

typedef struct eos_driver_t eos_driver_t;
struct eos_driver_t {
  char scope[EOS_XSMALL_STR_LEN];
  char name[EOS_XSMALL_STR_LEN];
  char devname[EOS_XSMALL_STR_LEN];
///////////////////////////////////////////////////////
// Dev operations:
///////////////////////////////////////////////////////
  bool (*init)(eos_dev_t *dev);
  void (*shutdown)(eos_dev_t *dev);
///////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// Basic IO operations:
///////////////////////////////////////////////////////
  int (*read)(eos_dev_t *dev, void *buf, size_t len);
  int (*write)(eos_dev_t *dev, void *buf, size_t len);
  int (*ioctl)(eos_dev_t *dev, int cmd, va_list args);
  off_t (*lseek)(eos_dev_t *dev, off_t offset, int whence);
///////////////////////////////////////////////////////
// DIR specific IO operations:
///////////////////////////////////////////////////////
  DIR *(*opendir)(eos_dev_t *dev, const char *name);
  // TODO: unify dirp/pdir name
  struct dirent *(*readdir)(eos_dev_t *dev, DIR *pdir);
  void (*seekdir)(eos_dev_t *dev, DIR *pdir, long offset);
  long (*telldir)(eos_dev_t *dev, DIR *pdir);
  int (*closedir)(eos_dev_t *dev, DIR *dirp);
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// Inform operations
///////////////////////////////////////////////////////
  bool (*attach_req)(eos_dev_t *dev, eos_dev_t *child);
  bool (*detach_req)(eos_dev_t *dev, eos_dev_t *child);
///////////////////////////////////////////////////////
};

//============================================(^_^)==\~

// Attribute to be used for static eos_driver_t allocation
#define EOS_DRIVER_ATTR __attribute__((section(".eos_drivers"))) const

extern const eos_driver_t _eos_drivers_start[];
extern const eos_driver_t _eos_drivers_end[];

#define EOS_DRIVER_INIT                                                        \
  .init = eos_driver_init_empty,                                               \
  .shutdown = eos_driver_shutdown_empty,                                       \
  .read = eos_driver_read_empty,                                               \
  .write = eos_driver_write_empty, .ioctl = eos_driver_ioctl_empty,            \
  .lseek = eos_driver_lseek_empty,                                             \
  .opendir = eos_driver_opendir_empty,                                         \
  .readdir = eos_driver_readdir_empty,                                         \
  .seekdir = eos_driver_seekdir_empty,                                         \
  .telldir = eos_driver_telldir_empty,                                         \
  .closedir = eos_driver_closedir_empty,                                       \
  .attach_req = eos_driver_attach_req_empty,                                   \
  .detach_req = eos_driver_detach_req_empty, .devname = ""

// Seeks for driver with particular scope/name/pair
eos_driver_t *eos_driver_find(const char *scope, const char *name);

// EMPTY:
bool eos_driver_init_empty(eos_dev_t *dev);

void eos_driver_shutdown_empty(eos_dev_t *dev);

int eos_driver_read_empty(eos_dev_t *dev, void *buf, size_t len);

int eos_driver_write_empty(eos_dev_t *dev, void *buf, size_t len);

int eos_driver_ioctl_empty(eos_dev_t *dev, int cmd, va_list args);

off_t eos_driver_lseek_empty(eos_dev_t *dev, off_t offset, int whence);

DIR * eos_driver_opendir_empty(eos_dev_t *dev, const char *name);

struct dirent *eos_driver_readdir_empty(eos_dev_t *dev, DIR *pdir);

void eos_driver_seekdir_empty(eos_dev_t *dev, DIR *pdir, long offset);

long eos_driver_telldir_empty(eos_dev_t *dev, DIR *pdir);

int eos_driver_closedir_empty(eos_dev_t *dev, DIR *dirp);

bool eos_driver_attach_req_empty(eos_dev_t *dev, eos_dev_t *child);

bool eos_driver_detach_req_empty(eos_dev_t *dev, eos_dev_t *child);

// DEFAULT:
int eos_driver_ioctl_default(eos_dev_t *dev, int cmd, va_list args);

// SPECIAL:

