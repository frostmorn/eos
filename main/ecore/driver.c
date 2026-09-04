#include "ecore/driver.h"
#include "ecore/dev.h"
#include "ecore/error.h"
#include "ecore/ioctl.h"
#include "emisc/fancymacro.h"
#include <errno.h>

// Most of calls specified here are wrappers around per driver implementations
// Them are used to provide basic functionality for driver VFS

// For reentry 
// TODO: Semaphores/Mutex for wrappers

// Since all those calls are shared for all drivers there's a sense to 
// TODO: place calls below in IRAM

bool eos_drv_init(eos_dev_t *dev) {

  if (dev && dev->driver && dev->driver->init)
    return dev->driver->init(dev);

  // Default behaviour
  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);
  return true;
}

void eos_drv_shutdown(eos_dev_t *dev) {
  if (dev && dev->driver && dev->driver->shutdown){
    dev->driver->shutdown(dev);
    return;
  }

  // Default behaviour
  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);
}

bool eos_drv_attach_req(eos_dev_t *dev, eos_dev_t *child){
  if (dev && dev->driver && dev->driver->attach_req)
    return dev->driver->attach_req(dev, child);

  // Default behaviour
  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);
  return true;
}

bool eos_drv_detach_req(eos_dev_t *dev, eos_dev_t *child){
  if (dev && dev->driver && dev->driver->detach_req)
    return dev->driver->detach_req(dev, child);

  // Default behaviour
  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);
  return true;
}

ssize_t eos_drv_write(eos_dev_t *dev, int fd, void *data, size_t size) {
  if (dev && dev->driver && dev->driver->write)
    return dev->driver->write(dev, fd, data, size);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  // Default behaviour
  errno = ENOSYS;
  return -1;
}

off_t eos_drv_lseek(eos_dev_t *dev, int fd, off_t offset, int whence) {
  if (dev && dev->driver && dev->driver->lseek)
    return dev->driver->lseek(dev, fd, offset, whence);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  // Default behaviour
  errno = ENOSYS;
  return -1;
}

ssize_t eos_drv_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  if (dev && dev->driver && dev->driver->read)
    return dev->driver->read(dev, fd, dst, size);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  // Default behaviour
  errno = ENOSYS;
  return -1;
}

ssize_t eos_drv_pread(eos_dev_t *dev, int fd, void *dst, size_t size, off_t offset) {
  if (dev && dev->driver && dev->driver->pread)
    return dev->driver->pread(dev, fd, dst, size, offset);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  // TODO: implement through lseek + read + lseek when mtx adds
  // Default behaviour
  errno = ENOSYS;
  return -1;
}

ssize_t eos_drv_pwrite(eos_dev_t *dev, int fd, void *src, size_t size, off_t offset) {
  if (dev && dev->driver && dev->driver->pwrite)
    return dev->driver->pwrite(dev, fd, src, size, offset);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  // TODO: implement through lseek + write + lseek when mtx adds
  // Default behaviour
  errno = ENOSYS;
  return -1;
}

int eos_drv_open(eos_dev_t *dev, const char *path, int flags, int mode){
  if (dev && dev->driver && dev->driver->open)
    return dev->driver->open(dev, path, flags, mode);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  // TODO: check flags and allow open with empty path
  errno = ENOSYS;
  return -1;
}

int eos_drv_close(eos_dev_t *dev, int fd){
  if (dev && dev->driver && dev->driver->close)
    return dev->driver->close(dev, fd);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  // TODO: check flags and allow close DEFAULT_DESCRIPTOR? 
  errno = ENOSYS;
  return -1;
}

int eos_drv_fstat(eos_dev_t *dev, int fd, struct stat *st){
  if (dev && dev->driver && dev->driver->fstat)
    return dev->driver->fstat(dev, fd, st);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_stat(eos_dev_t *dev, const char *path, struct stat *st){
  if (dev && dev->driver && dev->driver->stat)
    return dev->driver->stat(dev, path, st);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_link(eos_dev_t *dev, const char *n1, const char *n2){
  // TODO: theoretically we can support cross device linkage in case
  // we control all filesystems through proxy vfs, and implement
  // path resolution for each proxied vfs by own

  // This way we can intercept all calls, whose as u see vfs-specific,
  // but highly likely vfs resolution based on n1 path arg

  if (dev && dev->driver && dev->driver->link)
    return dev->driver->link(dev, n1, n2);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_unlink(eos_dev_t *dev, const char *path){
  // TODO: check eos_drv_link one

  if (dev && dev->driver && dev->driver->unlink)
    return dev->driver->unlink(dev, path);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_rename(eos_dev_t *dev, const char *src, const char *dst){

  // TODO: cross device rename, look at link/unlink
  if (dev && dev->driver && dev->driver->rename)
    return dev->driver->rename(dev, src, dst);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

DIR *eos_drv_opendir(eos_dev_t *dev, const char *path){
  if (dev && dev->driver && dev->driver->opendir)
    return dev->driver->opendir(dev, path);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return NULL;
}

struct dirent *eos_drv_readdir(eos_dev_t *dev, DIR *pdir){
  if (dev && dev->driver && dev->driver->readdir)
    return dev->driver->readdir(dev, pdir);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return NULL;
}

long eos_drv_telldir(eos_dev_t *dev, DIR *pdir){
  if (dev && dev->driver && dev->driver->telldir)
    return dev->driver->telldir(dev, pdir);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

void eos_drv_seekdir(eos_dev_t *dev, DIR *pdir, long offset){
  // TODO: implement through telldir++? hm, this is a dumb one
  if (dev && dev->driver && dev->driver->seekdir){
    dev->driver->seekdir(dev, pdir, offset);
    return;
  }

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
}

int eos_drv_closedir(eos_dev_t *dev, DIR *pdir){
  if (dev && dev->driver && dev->driver->closedir)
    return dev->driver->closedir(dev, pdir);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_mkdir(eos_dev_t *dev, const char *name, mode_t mode){
  if (dev && dev->driver && dev->driver->mkdir)
    return dev->driver->mkdir(dev, name, mode);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_rmdir(eos_dev_t *dev, const char *name){
  if (dev && dev->driver && dev->driver->rmdir)
    return dev->driver->rmdir(dev, name);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_fcntl(eos_dev_t *dev, int fd, int cmd, int arg){
  // RESEARCH: how does that differ from ioctl?
  if (dev && dev->driver && dev->driver->fcntl)
    return dev->driver->fcntl(dev, fd, cmd, arg);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args){

  // Provide cross driver ioctl functionality
  switch (cmd) {
  case EOS_IOCTL_GET_DEV:
    eos_dev_t **out = va_arg(args, eos_dev_t **);
    *out = dev;
    return 0;
  }

  // Still not handled, check driver implementation
  if (dev && dev->driver && dev->driver->ioctl)
    return dev->driver->ioctl(dev, fd, cmd, args);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_fsync(eos_dev_t *dev, int fd){
  if (dev && dev->driver && dev->driver->fsync)
    return dev->driver->fsync(dev, fd);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_access(eos_dev_t *dev, const char *path, int amode){
  if (dev && dev->driver && dev->driver->access)
    return dev->driver->access(dev, path, amode);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_truncate(eos_dev_t *dev, const char *path, off_t length){
  if (dev && dev->driver && dev->driver->truncate)
    return dev->driver->truncate(dev, path, length);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_ftruncate(eos_dev_t *dev, int fd, off_t length){
  if (dev && dev->driver && dev->driver->ftruncate)
    return dev->driver->ftruncate(dev, fd, length);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

int eos_drv_utime(eos_dev_t *dev, const char *path, const struct utimbuf *times){
  if (dev && dev->driver && dev->driver->utime)
    return dev->driver->utime(dev, path, times);

  EOS_LOGW("Call %s not implemented", __PRETTY_FUNCTION__);

  errno = ENOSYS;
  return -1;
}

eos_drv_t *eos_drv_find(const char *scope, const char *name) {
  for (const eos_drv_t *driver = _eos_drivers_start;
       driver < _eos_drivers_end; ++driver) {

    if (strcmp(scope, driver->scope) == 0 && strcmp(name, driver->name) == 0) {
      EOS_LOGI("Found driver %s/%s", scope, name);
      return (eos_drv_t *)driver;
    }
  }

  EOS_LOGE("Not found driver %s/%s", scope, name);
  assert(0);
  return NULL;
}
