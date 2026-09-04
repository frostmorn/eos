#pragma once
///////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////
#include <esp_vfs.h>
#include <dirent.h>
#include "emisc/strlimits.h"

///////////////////////////////////////////////////////
// Structure representing a device driver for EOS
///////////////////////////////////////////////////////

// Each driver represents ESP-IDF VFS initialized with 
// dev passed as ctx pointer at attach

// TODO: maybe it would change, for now just mark places
// DEFAULT FD if driver is meant to be used as a file
#define EOS_DRV_FD 0

typedef struct eos_dev_t eos_dev_t;

typedef struct eos_drv_t eos_drv_t;
struct eos_drv_t {
  char scope[EOS_XSMALL_STR_LEN];
  char name[EOS_XSMALL_STR_LEN];
  char devname[EOS_XSMALL_STR_LEN];
  uint32_t flags;
///////////////////////////////////////////////////////
// Dev operations:
///////////////////////////////////////////////////////
  bool (*init)(eos_dev_t *dev);
  void (*shutdown)(eos_dev_t *dev);
///////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// Inform operations
///////////////////////////////////////////////////////
  bool (*attach_req)(eos_dev_t *dev, eos_dev_t *child);
  bool (*detach_req)(eos_dev_t *dev, eos_dev_t *child);
///////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// VFS operations:
///////////////////////////////////////////////////////
  ssize_t (*write)(eos_dev_t * dev, int fd, const void * data, size_t size); 
  off_t (*lseek)(eos_dev_t *dev, int fd, off_t offset, int whence);
  ssize_t (*read)(eos_dev_t *dev, int fd, void *dst, size_t size);
  ssize_t (*pread)(eos_dev_t *dev, int fd, void *dst, size_t size, off_t offset);
  ssize_t (*pwrite)(eos_dev_t *dev, int fd, void *src, size_t size, off_t offset);
  int (*open)(eos_dev_t *dev, const char *path, int flags, int mode);
  int (*close)(eos_dev_t *dev, int fd);
  int (*fstat)(eos_dev_t *dev, int fd, struct stat *st);
  int (*stat)(eos_dev_t *dev, const char *path, struct stat *st);
  int (*link)(eos_dev_t *dev, const char *n1, const char *n2);
  int (*unlink)(eos_dev_t *dev, const char *path);
  int (*rename)(eos_dev_t *dev, const char *src, const char *dst);
  DIR *(*opendir)(eos_dev_t *dev, const char *path);
  struct dirent *(*readdir)(eos_dev_t *dev, DIR *pdir);
  long (*telldir)(eos_dev_t *dev, DIR *pdir);
  void (*seekdir)(eos_dev_t *dev, DIR *pdir, long offset);
  int (*closedir)(eos_dev_t *dev, DIR *pdir);
  int (*mkdir)(eos_dev_t *dev, const char *name, mode_t mode);
  int (*rmdir)(eos_dev_t *dev, const char *name);
  int (*fcntl)(eos_dev_t *dev, int fd, int cmd, int arg);
  int (*ioctl)(eos_dev_t *dev, int fd, int cmd, va_list args);
  int (*fsync)(eos_dev_t *dev, int fd);
  int (*access)(eos_dev_t *dev, const char *path, int amode);
  int (*truncate)(eos_dev_t *dev, const char *path, off_t length);
  int (*ftruncate)(eos_dev_t *dev, int fd, off_t length);
  int (*utime)(eos_dev_t *dev, const char *path, const struct utimbuf *times);
  // TERMIOS?  
};

//============================================(^_^)==\~

// Attribute to be used for static eos_drv_t allocation
#define EOS_DRV_ATTR __attribute__((section(".eos_drivers"))) const

extern const eos_drv_t _eos_drivers_start[];
extern const eos_drv_t _eos_drivers_end[];

// TODO: maybe we no need that
#define EOS_DRV_INIT                                                    	\
  .scope      = "undefined",                                                    \
  .name       = "undefined",                                                    \
  .devname    = "undefined",                                                    \
  .flags      = 0,                                                              \
  .init       = NULL,                                                           \
  .shutdown   = NULL,                                                           \
  .attach_req = NULL,                                                           \
  .detach_req = NULL,                                                           \
  .write      = NULL,                                                           \
  .lseek      = NULL,                                                           \
  .read       = NULL,                                                           \
  .pread      = NULL,                                                           \
  .pwrite     = NULL,                                                           \
  .open       = NULL,                                                           \
  .close      = NULL,                                                           \
  .fstat      = NULL,                                                           \
  .stat       = NULL,                                                           \
  .link       = NULL,                                                           \
  .unlink     = NULL,                                                           \
  .rename     = NULL,                                                           \
  .opendir    = NULL,                                                           \
  .readdir    = NULL,                                                           \
  .telldir    = NULL,                                                           \
  .seekdir    = NULL,                                                           \
  .closedir   = NULL,                                                           \
  .mkdir      = NULL,                                                           \
  .rmdir      = NULL,                                                           \
  .fcntl      = NULL,                                                           \
  .ioctl      = NULL,                                                           \
  .fsync      = NULL,                                                           \
  .access     = NULL,                                                           \
  .truncate   = NULL,                                                           \
  .ftruncate  = NULL,                                                           \
  .utime      = NULL

// Find suitable driver by scope/name
eos_drv_t *eos_drv_find(const char *scope, const char *name);


///////////////////////////////////////////////////////
// Dev operations:
///////////////////////////////////////////////////////
bool eos_drv_init(eos_dev_t *dev);
void eos_drv_shutdown(eos_dev_t *dev);
///////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// Inform operations
///////////////////////////////////////////////////////
bool eos_drv_attach_req(eos_dev_t *dev, eos_dev_t *child);
bool eos_drv_detach_req(eos_dev_t *dev, eos_dev_t *child);
///////////////////////////////////////////////////////


///////////////////////////////////////////////////////
// VFS operations:
///////////////////////////////////////////////////////
ssize_t eos_drv_write(eos_dev_t *dev, int fd, void *data, size_t size);
off_t eos_drv_lseek(eos_dev_t *dev, int fd, off_t offset, int whence);
ssize_t eos_drv_read(eos_dev_t *dev, int fd, void *dst, size_t size);
ssize_t eos_drv_pread(eos_dev_t *dev, int fd, void *dst, size_t size, off_t offset);
ssize_t eos_drv_pwrite(eos_dev_t *dev, int fd, void *src, size_t size, off_t offset);
int eos_drv_open(eos_dev_t *dev, const char *path, int flags, int mode);
int eos_drv_close(eos_dev_t *dev, int fd);
int eos_drv_fstat(eos_dev_t *dev, int fd, struct stat *st);
int eos_drv_stat(eos_dev_t *dev, const char *path, struct stat *st);
int eos_drv_link(eos_dev_t *dev, const char *n1, const char *n2);
int eos_drv_unlink(eos_dev_t *dev, const char *path);
int eos_drv_rename(eos_dev_t *dev, const char *src, const char *dst);
DIR *eos_drv_opendir(eos_dev_t *dev, const char *path);
struct dirent *eos_drv_readdir(eos_dev_t *dev, DIR *pdir);
long eos_drv_telldir(eos_dev_t *dev, DIR *pdir);
void eos_drv_seekdir(eos_dev_t *dev, DIR *pdir, long offset);
int eos_drv_closedir(eos_dev_t *dev, DIR *pdir);
int eos_drv_mkdir(eos_dev_t *dev, const char *name, mode_t mode);
int eos_drv_rmdir(eos_dev_t *dev, const char *name);
int eos_drv_fcntl(eos_dev_t *dev, int fd, int cmd, int arg);
int eos_drv_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args);
int eos_drv_fsync(eos_dev_t *dev, int fd);
int eos_drv_access(eos_dev_t *dev, const char *path, int amode);
int eos_drv_truncate(eos_dev_t *dev, const char *path, off_t length);
int eos_drv_ftruncate(eos_dev_t *dev, int fd, off_t length);
int eos_drv_utime(eos_dev_t *dev, const char *path, const struct utimbuf *times);
///////////////////////////////////////////////////////