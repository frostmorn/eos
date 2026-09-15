#ifdef EOS_DRV_$DRIVER_SCOPE:upper$_$DRIVER_NAME:upper$_ENABLED
#include <errno.h>
#include <dirent.h>
#include "ecore/dev.h"
#include "ecore/ioctl.h"
///////////////////////////////////////////////////////////////////////
// $DRIVER_NAME$ DRIVER FOR EOS
///////////////////////////////////////////////////////////////////////
// $DRIVER_DESC$
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// DRIVER DATA:
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// DRIVER IMPLEMENTATION:
///////////////////////////////////////////////////////////////////////
//=====================================================================
//bool driver_$DRIVER_SCOPE$_$DRIVER_NAME$_init(
//  eos_dev_t   *dev
//){}
//=====================================================================
//bool driver_$DRIVER_SCOPE$_$DRIVER_NAME$_shutdown(
//  eos_dev_t   *dev
//){}
//=====================================================================
//bool driver_$DRIVER_SCOPE$_$DRIVER_NAME$_attach_req(
//  eos_dev_t   *dev, 
//  eos_dev_t   *child
//){}
//=====================================================================
//bool driver_$DRIVER_SCOPE$_$DRIVER_NAME$_detach_req(
//  eos_dev_t   *dev, 
//  eos_dev_t   *child
//){}
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_open(
//  eos_dev_t   *dev, 
//  const char  *path, 
//  int         flags,
//  int         mode
//){}
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_close$(
//  eos_dev_t   *dev, 
//  int         fd
//){}
//=====================================================================
//ssize_t driver_$DRIVER_SCOPE$_$DRIVER_NAME$_read(
//  eos_dev_t   *dev, 
//  int         fd, 
//  void        *dst, 
//  size_t      size
//){}
//=====================================================================
//ssize_t driver_$DRIVER_SCOPE$_$DRIVER_NAME$_write(
//  eos_dev_t   *dev, 
//  int         fd, 
//  void        *data, 
//  size_t      size
//){}
//=====================================================================
//off_t driver_$DRIVER_SCOPE$_$DRIVER_NAME$_lseek(
//  eos_dev_t   *dev, 
//  int         fd, 
//  off_t       offset, 
//  int         whence
//);
//=====================================================================
//ssize_t driver_$DRIVER_SCOPE$_$DRIVER_NAME$_pread(
//  eos_dev_t   *dev, 
//  int         fd, 
//  void        *dst, 
//  size_t      size, 
//  off_t       offset
//);
//=====================================================================
//ssize_t driver_$DRIVER_SCOPE$_$DRIVER_NAME$_pwrite(
//  eos_dev_t   *dev, 
//  int         fd, 
//  void        *src, 
//  size_t      size, 
//  off_t       offset
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_fstat(
//  eos_dev_t   *dev, 
//  int         fd, 
//  struct stat *st
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_stat(
//  eos_dev_t   *dev, 
//  const char  *path, 
//  struct stat *st
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_link(
//  eos_dev_t  *dev, 
//  const char *n1, 
//  const char *n2
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_unlink(
//  eos_dev_t *dev, 
//  const char *path
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_rename(
//  eos_dev_t *dev, 
//  const char *src, 
//  const char *dst
//);
//=====================================================================
//DIR *driver_$DRIVER_SCOPE$_$DRIVER_NAME$_opendir(
//  eos_dev_t *dev, 
//  const char *path
//);
//=====================================================================
//struct dirent *driver_$DRIVER_SCOPE$_$DRIVER_NAME$_readdir(
//  eos_dev_t *dev, 
//  DIR *pdir
//);
//=====================================================================
//long driver_$DRIVER_SCOPE$_$DRIVER_NAME$_telldir(
//  eos_dev_t *dev, 
//  DIR *pdir
//);
//=====================================================================
//void driver_$DRIVER_SCOPE$_$DRIVER_NAME$_seekdir(
//  eos_dev_t *dev, 
//  DIR *pdir, 
//  long offset
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_closedir(
//  eos_dev_t *dev, 
//  DIR *pdir);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_mkdir(
//  eos_dev_t *dev, 
//  const char *name, 
//  mode_t mode
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_rmdir(
//  eos_dev_t *dev, 
//  const char *name
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_fcntl(
//  eos_dev_t *dev, 
//  int fd, 
//  int cmd, 
//  int arg
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_ioctl(
//  eos_dev_t *dev, 
//  int fd, 
//  int cmd, 
//  va_list args
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_fsync(
//  eos_dev_t *dev, 
//  int fd
// );
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_access(
//  eos_dev_t *dev, 
//  const char *path, 
//  int amode
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_truncate(
//  eos_dev_t *dev, 
//  const char *path, 
//  off_t length
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_ftruncate(
//  eos_dev_t *dev, 
//  int fd, 
//  off_t length
//);
//=====================================================================
//int driver_$DRIVER_SCOPE$_$DRIVER_NAME$_utime(
//  eos_dev_t *dev, 
//  const char *path, 
//  const struct utimbuf *times
//);
//=====================================================================
///////////////////////////////////////////////////////////////////////
// DRIVER MANIFEST:
///////////////////////////////////////////////////////////////////////
EOS_DRV_ATTR driver_$DRIVER_SCOPE$_$DRIVER_NAME$_t driver_$DRIVER_SCOPE$_$DRIVER_NAME$ = {
  EOS_DRV_INIT,
  .flags = $DRIVER_FLAGS$,
  .scope = "$DRIVER_SCOPE$",
  .name = "$DRIVER_NAME$",
  .devname = "$DRIVER_DEVNAME$",
//  .init       = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_init,
//  .shutdown   = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_shutdown,
//  .attach_req = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_attach_req,
//  .detach_req = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_detach_req,
//  .write      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_write,
//  .lseek      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_lseek,
//  .read       = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_read,
//  .pread      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_pread,
//  .pwrite     = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_pwrite,
//  .open       = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_open, 
//  .close      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_close,
//  .fstat      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_fstat,
//  .stat       = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_stat,
//  .link       = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_link,
//  .unlink     = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_unlink,
//  .rename     = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_rename,
//  .opendir    = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_opendir,
//  .readdir    = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_readdir,
//  .telldir    = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_telldir,
//  .seekdir    = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_seekdir,
//  .closedir   = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_closedir,
//  .mkdir      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_mkdir,
//  .rmdir      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_rmdir,
//  .fcntl      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_fcntl,
//  .ioctl      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_ioctl,
//  .fsync      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_fsync,
//  .access     = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_access,
//  .truncate   = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_truncate,
//  .ftruncate  = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_ftruncate,
//  .utime      = driver_$DRIVER_SCOPE$_$DRIVER_NAME$_utime
};
///////////////////////////////////////////////////////////////////////

#endif

// Generated on $_DATE$
// GIT HEAD at $_COMMIT$ 

// (^__^)==\~
